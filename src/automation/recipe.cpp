#include "automation/recipe.hpp"
#include "automation/inspection_validation.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <set>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw InspectionError(code, message);
}
QJsonObject recipeSchema() {
    const QJsonObject step{
        {"type", "object"},
        {"properties", QJsonObject{{"id", QJsonObject{{"type", "string"},
                                                      {"pattern", "^[A-Za-z0-9_-]+$"},
                                                      {"minLength", 1},
                                                      {"maxLength", 64}}},
                                   {"request", QJsonObject{{"type", "object"}}}}},
        {"required", QJsonArray{"id", "request"}},
        {"additionalProperties", false}};
    return {{"$schema", "https://json-schema.org/draft/2020-12/schema"},
            {"type", "object"},
            {"properties", QJsonObject{{"apiVersion", QJsonObject{{"const", 1}}},
                                       {"steps", QJsonObject{{"type", "array"},
                                                             {"minItems", 1},
                                                             {"maxItems", 1000},
                                                             {"items", step}}}}},
            {"required", QJsonArray{"apiVersion", "steps"}},
            {"additionalProperties", false}};
}
struct Reference {
    QString step;
    QStringList path;
};
Reference reference(const QJsonValue &value) {
    if (!value.isString() || value.toString().size() > 512)
        fail("INVALID_REFERENCE", "Reference must be a string of at most 512 characters");
    const auto text = value.toString();
    const auto separator = text.indexOf('#');
    if (separator <= 0)
        fail("INVALID_REFERENCE", "Reference requires previous-step# followed by a JSON pointer");
    Reference ref;
    ref.step = text.left(separator);
    const auto pointer = text.mid(separator + 1);
    if (pointer.isEmpty())
        return ref;
    if (!pointer.startsWith('/'))
        fail("INVALID_REFERENCE", "Reference path must be an empty or slash-prefixed JSON pointer");
    for (const auto &encoded : pointer.mid(1).split('/')) {
        QString decoded;
        for (qsizetype i = 0; i < encoded.size(); ++i) {
            if (encoded[i] != '~')
                decoded += encoded[i];
            else {
                if (++i >= encoded.size() || (encoded[i] != '0' && encoded[i] != '1'))
                    fail("INVALID_REFERENCE", "JSON pointer escape must be ~0 or ~1");
                decoded += encoded[i] == '0' ? '~' : '/';
            }
        }
        ref.path.append(decoded);
    }
    return ref;
}
void validateTemplate(const QJsonValue &value, const std::set<QString> &previous, int depth = 0) {
    if (depth > 64)
        fail("LIMIT_EXCEEDED", "Recipe template nesting exceeds 64 levels");
    if (value.isObject()) {
        const auto fields = value.toObject();
        if (fields.contains("$ref") || fields.contains("$literal")) {
            if (fields.size() != 1)
                fail("INVALID_REFERENCE", "Template reference/literal tags must be the only field");
            if (fields.contains("$ref") && !previous.contains(reference(fields["$ref"]).step))
                fail("INVALID_REFERENCE", "Recipe references must target an earlier step");
            return;
        }
        for (auto i = fields.begin(); i != fields.end(); ++i)
            validateTemplate(i.value(), previous, depth + 1);
    } else if (value.isArray())
        for (const auto &item : value.toArray())
            validateTemplate(item, previous, depth + 1);
}
QJsonValue resolve(const QJsonValue &value, const std::map<QString, QJsonObject> &responses,
                   size_t &remaining, int depth = 0) {
    auto charge = [&](size_t count) {
        if (count > remaining)
            fail("LIMIT_EXCEEDED", "Resolved recipe request exceeds 64 KiB");
        remaining -= count;
    };
    auto scalar = [&](const QJsonValue &item) {
        charge(size_t(QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact).size() - 2));
        return item;
    };
    if (depth > 64)
        fail("LIMIT_EXCEEDED", "Resolved request nesting exceeds 64 levels");
    if (value.isObject()) {
        const auto fields = value.toObject();
        if (fields.contains("$literal"))
            return scalar(fields["$literal"]);
        if (fields.contains("$ref")) {
            const auto ref = reference(fields["$ref"]);
            const auto found = responses.find(ref.step);
            if (found == responses.end())
                fail("INVALID_REFERENCE", "Referenced response is unavailable");
            QJsonValue result = found->second;
            for (const auto &key : ref.path) {
                if (result.isObject()) {
                    const auto object = result.toObject();
                    if (!object.contains(key))
                        fail("INVALID_REFERENCE", "Referenced object property does not exist");
                    result = object[key];
                } else if (result.isArray()) {
                    const auto array = result.toArray();
                    bool ok = false;
                    const auto index = key.toULongLong(&ok);
                    if (!ok || QString::number(index) != key || index >= quint64(array.size()))
                        fail("INVALID_REFERENCE", "Referenced array index is outside its bounds");
                    result = array[qsizetype(index)];
                } else
                    fail("INVALID_REFERENCE", "Cannot traverse a scalar response value");
            }
            return scalar(result);
        }
        charge(2);
        QJsonObject result;
        for (auto i = fields.begin(); i != fields.end(); ++i) {
            scalar(i.key());
            charge(2);
            result[i.key()] = resolve(i.value(), responses, remaining, depth + 1);
        }
        return result;
    }
    if (value.isArray()) {
        charge(2);
        QJsonArray result;
        for (const auto &item : value.toArray()) {
            charge(1);
            result.append(resolve(item, responses, remaining, depth + 1));
        }
        return result;
    }
    return scalar(value);
}
} // namespace
QJsonObject recipeCapabilities() {
    return {{"apiVersion", 1},
            {"schema", recipeSchema()},
            {"limits", QJsonObject{{"inputBytes", recipeInputBytes},
                                   {"steps", 1000},
                                   {"depth", 64},
                                   {"retainedBytes", recipeBudgetBytes},
                                   {"outputBytes", recipeBudgetBytes},
                                   {"responseReservationBytes", sessionResponseBytes}}},
            {"references", "Exact {$ref: previous-step#/result/path} objects use typed JSON "
                           "pointers; {$literal: value} suppresses expansion"},
            {"atomicity", "Each transaction commits independently; later failure preserves earlier "
                          "commits and saves"},
            {"execution", "Sequential registered session requests; stops at the first failure"},
            {"expressions", false},
            {"shell", false}};
}
AutomationRecipe AutomationRecipe::parse(const QByteArray &bytes) {
    if (bytes.size() > recipeInputBytes)
        fail("LIMIT_EXCEEDED", "Recipe exceeds 1 MiB");
    checkAutomationDepth(bytes);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        fail("INVALID_REQUEST", "Recipe must be one JSON object");
    const auto object = json.object();
    if (object["apiVersion"] != 1)
        fail("UNSUPPORTED_VERSION", "Recipe requires API version 1");
    inspection_detail::validateParameters(object, recipeSchema());
    const auto steps = object["steps"].toArray();
    std::set<QString> previous;
    for (const auto &value : steps) {
        const auto step = value.toObject();
        const auto id = step["id"].toString();
        if (!QRegularExpression("\\A[A-Za-z0-9_-]{1,64}\\z").match(id).hasMatch() ||
            previous.contains(id))
            fail("INVALID_REQUEST", "Recipe step IDs must be unique ASCII identifiers");
        validateTemplate(step["request"], previous);
        previous.insert(id);
    }
    return AutomationRecipe(steps);
}
AutomationRecipe AutomationRecipe::load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("INPUT_ERROR", "Cannot open the explicitly selected recipe");
    const auto bytes = file.read(recipeInputBytes + 1);
    if (file.error() != QFileDevice::NoError)
        fail("INPUT_ERROR", "Could not read recipe bytes");
    return parse(bytes);
}
int AutomationRecipe::run(AutomationSession &session, QIODevice &output) const {
    return run(session, output, Limits{});
}
int AutomationRecipe::run(AutomationSession &session, QIODevice &output, Limits limits) const {
    if (!limits.retainedBytes || limits.retainedBytes > recipeBudgetBytes ||
        limits.outputBytes < 16 * 1024 || limits.outputBytes > recipeBudgetBytes)
        fail("INVALID_REQUEST", "Invalid recipe resource limits");
    std::map<QString, QJsonObject> responses;
    size_t retained = 0, emitted = 0;
    QString current;
    try {
        for (const auto &value : steps_) {
            const auto step = value.toObject();
            current = step["id"].toString();
            // Reserve a complete maximum-sized reply before allowing effects.
            if (retained + 2 * sessionResponseBytes + 512 > limits.retainedBytes ||
                emitted + sessionResponseBytes + 16 * 1024 > limits.outputBytes)
                fail("LIMIT_EXCEEDED",
                     "Recipe response budget exhausted before the next operation");
            size_t remaining = transactionRequestBytes;
            const auto request = resolve(step["request"], responses, remaining);
            if (!request.isObject())
                fail("INVALID_REFERENCE", "Resolved step request must be an object");
            const auto encoded = QJsonDocument(request.toObject()).toJson(QJsonDocument::Compact);
            if (encoded.size() > transactionRequestBytes)
                fail("LIMIT_EXCEEDED", "Resolved recipe request exceeds 64 KiB");
            checkAutomationDepth(encoded);
            auto response = session.respond({{"id", current}, {"request", request}});
            const auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact).size();
            writeAutomationResponse(output, response);
            emitted += size_t(bytes) + 1;
            if (response["ok"] != true) {
                session.close();
                return 1;
            }
            responses.emplace(current, std::move(response));
            retained += size_t(bytes) * 2 + 512;
        }
        session.close();
        return 0;
    } catch (const std::exception &error) {
        try {
            writeAutomationResponse(output, {{"id", current.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                      : QJsonValue(current)},
                                             {"ok", false},
                                             {"error", automationFailure(error)}});
        } catch (...) {
        }
        try {
            session.close();
        } catch (...) {
        }
        return 1;
    }
}
} // namespace sketchy

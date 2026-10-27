#include "app/quantities_job.hpp"
#include "automation/inspection.hpp"
#include <algorithm>
#include <exception>

namespace sketchy {

ReportJob::ReportJob(Work work, QObject *parent) : QThread(parent), work_(std::move(work)) {}

ReportJob::~ReportJob() {
    requestCancel();
    wait();
}

void ReportJob::run() {
    ReportHooks hooks;
    hooks.cancel = &cancel_;
    std::size_t reported = 0;
    hooks.progress = [this, &reported](std::size_t done, std::size_t total) {
        // About two hundred updates per run, and the last one. The engine calls this on this thread;
        // the signal reaches the panel through a queued connection.
        const auto step = std::max<std::size_t>(1, total / 200);
        if (done != total && done - reported < step)
            return;
        reported = done;
        emit progressed(qulonglong(done), qulonglong(total));
    };
    try {
        data_ = work_(hooks);
    } catch (const ReportCancelled &) {
        cancelled_ = true;
        code_ = "cancelled";
        message_ = "The report was cancelled";
    } catch (const InspectionError &error) {
        code_ = QString::fromStdString(error.code());
        message_ = QString::fromUtf8(error.what());
    } catch (const std::exception &error) {
        code_ = "error";
        message_ = QString::fromUtf8(error.what());
    } catch (...) {
        code_ = "error";
        message_ = QStringLiteral("The totals could not be computed");
    }
}

} // namespace sketchy

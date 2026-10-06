#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <wayland-client.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
struct Server {
    wl_resource *surface{}, *keyboard{};
    bool sent{};
};
static void send(Server *s) {
    if (s->surface && s->keyboard && !s->sent) {
        wl_array keys;
        wl_array_init(&keys);
        wl_keyboard_send_enter(s->keyboard, 1, s->surface, &keys);
        wl_array_release(&keys);
        s->sent = true;
    }
}
static void destroy(wl_client *, wl_resource *resource) { wl_resource_destroy(resource); }
static const struct wl_surface_interface surfaceMethods = {.destroy = destroy};
static void createSurface(wl_client *client, wl_resource *compositor, uint32_t id) {
    auto *s = static_cast<Server *>(wl_resource_get_user_data(compositor));
    s->surface = wl_resource_create(client, &wl_surface_interface, 1, id);
    wl_resource_set_implementation(s->surface, &surfaceMethods, nullptr, nullptr);
    send(s);
}
static const struct wl_compositor_interface compositorMethods = {.create_surface = createSurface};
static void bindCompositor(wl_client *client, void *data, uint32_t, uint32_t id) {
    auto *resource = wl_resource_create(client, &wl_compositor_interface, 1, id);
    wl_resource_set_implementation(resource, &compositorMethods, data, nullptr);
}
static void getKeyboard(wl_client *client, wl_resource *seat, uint32_t id) {
    auto *s = static_cast<Server *>(wl_resource_get_user_data(seat));
    s->keyboard = wl_resource_create(client, &wl_keyboard_interface, 1, id);
    send(s);
}
static const struct wl_seat_interface seatMethods = {.get_keyboard = getKeyboard};
static void bindSeat(wl_client *client, void *data, uint32_t, uint32_t id) {
    auto *resource = wl_resource_create(client, &wl_seat_interface, 1, id);
    wl_resource_set_implementation(resource, &seatMethods, data, nullptr);
}
struct Client {
    wl_compositor *compositor{};
    wl_seat *seat{};
    bool received{};
    bool expectDestroyed{true};
};
static void global(void *data, wl_registry *registry, uint32_t id, const char *interface,
                   uint32_t) {
    auto *c = static_cast<Client *>(data);
    if (!std::strcmp(interface, "wl_compositor"))
        c->compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, id, &wl_compositor_interface, 1));
    if (!std::strcmp(interface, "wl_seat"))
        c->seat = static_cast<wl_seat *>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
}
static void removed(void *, wl_registry *, uint32_t) {}
static const wl_registry_listener registryListener = {global, removed};
static void entered(void *data, wl_keyboard *, uint32_t, wl_surface *surface, wl_array *) {
    static_cast<Client *>(data)->received = true;
    assert((surface == nullptr) == static_cast<Client *>(data)->expectDestroyed);
    std::cerr << "Queued keyboard.enter surface: " << (surface ? "live" : "null after destruction")
              << "\n";
}
static const wl_keyboard_listener keyboardListener = {.enter = entered};
int main(int argc, char **) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    auto *server = wl_display_create();
    assert(server);
    Server state;
    wl_global_create(server, &wl_compositor_interface, 1, &state, bindCompositor);
    wl_global_create(server, &wl_seat_interface, 1, &state, bindSeat);
    assert(wl_client_create(server, sockets[0]));
    std::atomic_bool done{};
    std::thread worker([&] {
        while (!done) {
            wl_event_loop_dispatch(wl_display_get_event_loop(server), 10);
            wl_display_flush_clients(server);
        }
    });
    Client client;
    client.expectDestroyed = argc == 1;
    auto *display = wl_display_connect_to_fd(sockets[1]);
    assert(display);
    auto *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, &client);
    assert(wl_display_roundtrip(display) >= 0);
    assert(client.compositor && client.seat);
    auto *surface = wl_compositor_create_surface(client.compositor);
    auto *keyboard = wl_seat_get_keyboard(client.seat);
    auto *queue = wl_display_create_queue(display);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(keyboard), queue);
    wl_keyboard_add_listener(keyboard, &keyboardListener, &client);
    assert(wl_display_roundtrip(display) >= 0);
    assert(!client.received);
    if (client.expectDestroyed)
        wl_surface_destroy(surface);
    assert(wl_display_dispatch_queue_pending(display, queue) >= 0);
    assert(client.received);
    if (!client.expectDestroyed)
        wl_surface_destroy(surface);
    wl_keyboard_destroy(keyboard);
    wl_event_queue_destroy(queue);
    wl_seat_destroy(client.seat);
    wl_compositor_destroy(client.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    done = true;
    worker.join();
    wl_display_destroy_clients(server);
    wl_display_destroy(server);
    std::cerr << "Client/server and every owned proxy destroyed\n";
}

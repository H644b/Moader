// Cross-link check only. This ELF is never packaged or installed on the console.
#include "moader/library.hpp"
#include "moader/web.hpp"
int main(int argc, char** argv) {
    moader::UnavailableBackend backend;
    moader::Library library("sdmc:/moader-linkcheck", backend);
    moader::WebServer server(library, "sdmc:/moader-linkcheck", "012345", {});
    if (argc > 4) { server.start("127.0.0.1", 0); server.stop(); }
    if (argc > 3) {
        library.import_zip("0400f43008c44001", "linkcheck", argv[1], argv[2]);
        library.set_enabled("0400f43008c44001", "linkcheck", false);
        library.remove("0400f43008c44001", "linkcheck");
    }
    return library.snapshot_json().empty() || library.events_json().empty();
}

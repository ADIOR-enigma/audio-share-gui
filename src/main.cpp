#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>

/*  GLOBAL STATE  */

static std::atomic<bool> server_running{false};
static std::atomic<bool> stop_requested{false};
static std::atomic<pid_t> server_pid{-1};

static std::thread server_thread;
static int server_fd = -1;

static GtkWindow*     main_window = nullptr;
static GtkTextView*   log_view    = nullptr;
static GtkTextBuffer* log_buffer  = nullptr;
static AppIndicator*  indicator   = nullptr;

/*  LOGGING  */

static gboolean log_idle(gpointer data) {
    auto* s = static_cast<std::string*>(data);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buffer, &end);

    gtk_text_buffer_insert(log_buffer, &end, s->c_str(), -1);
    gtk_text_buffer_insert(log_buffer, &end, "\n", -1);

    GtkTextMark* mark =
        gtk_text_buffer_get_insert(log_buffer);
    gtk_text_view_scroll_mark_onscreen(log_view, mark);

    delete s;
    return G_SOURCE_REMOVE;
}

static void log_line(const std::string& s) {
    g_idle_add(log_idle, new std::string(s));
}

/*  PORT CHECK  */

static bool port_in_use(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return true;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool in_use = (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0);

    close(sock);
    return in_use;
}

/*  NETWORK  */

static std::set<std::string> default_ifaces() {
    std::set<std::string> r;
    std::ifstream f("/proc/net/route");
    std::string iface;
    unsigned long dest;
    f.ignore(4096, '\n');
    while (f >> iface >> std::hex >> dest) {
        if (dest == 0) r.insert(iface);
        f.ignore(4096, '\n');
    }
    return r;
}

static void populate_interfaces(GtkComboBoxText* combo) {
    gtk_combo_box_text_remove_all(combo);
    auto valid = default_ifaces();

    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr)) return;

    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!valid.count(ifa->ifa_name)) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,
                  &((sockaddr_in*)ifa->ifa_addr)->sin_addr,
                  ip, sizeof(ip));

        gtk_combo_box_text_append_text(
            combo,
            (std::string(ifa->ifa_name) + " — " + ip).c_str()
        );
    }
    freeifaddrs(ifaddr);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

/* HELPERS  */
static std::string as_cmd_path() {
    const char* appdir = g_getenv("APPDIR");
    if (!appdir) return "as-cmd"; // dev fallback
    return std::string(appdir) + "/usr/bin/as-cmd";
}

static std::string run_cmd(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    return out;
}

static void populate_encodings(GtkComboBoxText* combo) {
    gtk_combo_box_text_remove_all(combo);

    gtk_combo_box_text_append(combo, "default", "default");

    auto out = run_cmd(as_cmd_path() + " --list-encoding");

    std::istringstream ss(out);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.find("PCM") != std::string::npos) {
            std::string key;
            std::istringstream(line) >> key;

            gtk_combo_box_text_append(combo, key.c_str(), key.c_str());
        }
    }

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), "default");
}

static void populate_endpoints(GtkComboBoxText* combo) {
    gtk_combo_box_text_remove_all(combo);
    gtk_combo_box_text_append(combo, "default", "default");

    auto out = run_cmd(as_cmd_path() + " -l");

    std::istringstream ss(out);
    std::string line;

    while (std::getline(ss, line)) {
        auto idp = line.find("id:");
        auto np  = line.find("name:");
        if (idp == std::string::npos || np == std::string::npos)
            continue;

        std::string id =
            line.substr(idp + 3, np - (idp + 3));
        std::string name =
            line.substr(np + 5);

        // trim
        id.erase(0, id.find_first_not_of(" \t"));
        id.erase(id.find_last_not_of(" \t") + 1);

        gtk_combo_box_text_append(
            combo,
            id.c_str(),
            name.c_str()
        );
    }

    gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), "default");
}

/*  SERVER  */

struct ServerArgs {
    GtkComboBoxText* host;
    GtkEntry*        port;
    GtkComboBoxText* endpoint;
    GtkComboBoxText* encoding;
    GtkCheckButton*  verbose;
};

static std::string build_cmd(ServerArgs* a) {
    gchar* h = gtk_combo_box_text_get_active_text(a->host);
    if (!h) return "";

    std::string host = h;
    g_free(h);
    host = host.substr(host.find_last_of(' ') + 1);

    int port = atoi(gtk_entry_get_text(a->port));
    if (port_in_use(port)) {
        log_line("[ERROR] Port already in use");
        return "";
    }

    std::ostringstream cmd;
    cmd << as_cmd_path() << " --bind=" << host << ":" << port;

    if (auto* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->endpoint))) {
        if (strcmp(id, "default") != 0)
            cmd << " --endpoint=" << id;
    }

    if (auto* enc = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->encoding))) {
        if (strcmp(enc, "default") != 0)
            cmd << " --encoding=" << enc;
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(a->verbose)))
        cmd << " --verbose";

    return cmd.str();
}

static void server_worker(std::string cmd) {
    int pipefd[2];
    pipe(pipefd);

    // make read end non-blocking
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

    server_pid = fork();

    if (server_pid == 0) {
        setsid();
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(1);
    }

    close(pipefd[1]);

    struct pollfd pfd{};
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;

    char buf[512];

    while (true) {
        int ret;
        do {
            ret = poll(&pfd, 1, 200);
        } while (ret < 0 && errno == EINTR);// 200ms tick

        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0;
                log_line(buf);
            }
        }

        // child exited?
        int status;
        if (waitpid(server_pid, &status, WNOHANG) > 0)
            break;

        // stop requested?
        if (stop_requested) {
            kill(-server_pid, SIGINT);
            waitpid(server_pid, nullptr, 0);
            break;
        }
    }

    close(pipefd[0]);

    server_pid = -1;
    server_running = false;
    stop_requested = false;

    log_line("[INFO] Server stopped");
}

static void stop_server(GtkButton* btn) {
    if (!server_running || server_pid <= 0)
        return;
    if (!stop_requested.exchange(true)) {
        gtk_button_set_label(btn, "Start Server");
    }
}

static void start_stop(GtkButton* btn, gpointer data) {
    auto* args = static_cast<ServerArgs*>(data);

    if (server_running) {
        stop_server(btn);
        return;
    }

    auto cmd = build_cmd(args);
    if (cmd.empty()) return;

    server_running = true;
    gtk_button_set_label(btn, "Stop Server");
    log_line("[INFO] Starting server");
    log_line("[DEBUG] CMD: " + cmd);

    server_thread = std::thread(server_worker, cmd);
    server_thread.detach();
}

/*  TRAY  */

static GtkWidget* tray_menu(ServerArgs* args) {
    GtkWidget* menu = gtk_menu_new();

    auto* show   = gtk_menu_item_new_with_label("Show");
    auto* reload = gtk_menu_item_new_with_label("Reload");
    auto* quit   = gtk_menu_item_new_with_label("Quit");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), show);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reload);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);

    g_signal_connect(show, "activate",
        G_CALLBACK(+[](GtkMenuItem*, gpointer){
            gtk_widget_show(GTK_WIDGET(main_window));
            gtk_window_present(main_window);
        }), nullptr);

    g_signal_connect(reload, "activate",
        G_CALLBACK(+[](GtkMenuItem*, gpointer d){
            auto* a = static_cast<ServerArgs*>(d);
            populate_interfaces(a->host);
            populate_endpoints(a->endpoint);
            populate_encodings(a->encoding);
            log_line("[INFO] Reloaded system data");
        }), args);

    g_signal_connect(quit, "activate",
        G_CALLBACK(+[](GtkMenuItem*, gpointer d){
            stop_requested = true;

            // Snapshot PID safely
            pid_t pid = server_pid.load();
            if (pid > 0) {
                kill(-pid, SIGINT);
            }

            gtk_main_quit();
        }),
        args
    );

    gtk_widget_show_all(menu);
    return menu;
}

/*  MAIN  */

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    const char* appdir = g_getenv("APPDIR");
    if (!appdir) {
        g_error("APPDIR not set");
    }
    if (appdir) {
        std::string icon_path =
            std::string(appdir) + "/usr/share/icons/hicolor/256x256/apps";

        GtkIconTheme* theme = gtk_icon_theme_get_default();
        gtk_icon_theme_append_search_path(theme, icon_path.c_str());
    }

    std::string ui_path =
        std::string(appdir) +
        "/usr/share/ui/main.ui";
    GtkBuilder* b = gtk_builder_new_from_file(ui_path.c_str());

    main_window = GTK_WINDOW(gtk_builder_get_object(b, "main_window"));

    auto* args = new ServerArgs{
        GTK_COMBO_BOX_TEXT(gtk_builder_get_object(b, "host_dropdown")),
        GTK_ENTRY(gtk_builder_get_object(b, "port_entry")),
        GTK_COMBO_BOX_TEXT(gtk_builder_get_object(b, "endpoint_combo")),
        GTK_COMBO_BOX_TEXT(gtk_builder_get_object(b, "encoding_combo")),
        GTK_CHECK_BUTTON(gtk_builder_get_object(b, "verbose_check"))
    };

    auto* start_btn = GTK_BUTTON(gtk_builder_get_object(b, "start_button"));
    auto* close_btn = GTK_BUTTON(gtk_builder_get_object(b, "close_button"));

    log_view = GTK_TEXT_VIEW(gtk_builder_get_object(b, "log_view"));
    log_buffer = gtk_text_view_get_buffer(log_view);

    /* App logo forced size */
    auto* logo = GTK_IMAGE(gtk_builder_get_object(b, "app_logo"));

    std::string logo_path;

    if (appdir) {
        logo_path = std::string(appdir) +
            "/usr/share/icons/hicolor/256x256/apps/audio-share-gui.png";
    } else {
        // fallback for dev runs
        logo_path = "audio-share-gui.png";
    }

    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(
        logo_path.c_str(),
        18, 18,
        TRUE,
        &err
    );

    if (!pb) {
        g_warning("Failed to load logo: %s", err->message);
        g_clear_error(&err);
    } else {
        gtk_image_set_from_pixbuf(GTK_IMAGE(logo), pb);
        // DO NOT unref pb
    }

    populate_interfaces(args->host);
    populate_endpoints(args->endpoint);
    populate_encodings(args->encoding);

    g_signal_connect(
        main_window,
        "delete-event",
        G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer){
            // Prevent GTK from destroying the window
            gtk_widget_hide(GTK_WIDGET(main_window));
            return TRUE;
        }),
        nullptr
    );
    g_signal_connect(start_btn, "clicked", G_CALLBACK(start_stop), args);
    g_signal_connect(close_btn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer){
            gtk_widget_hide(GTK_WIDGET(main_window));
        }), nullptr);

    std::string tray_icon;

    if (appdir) {
        tray_icon =
            std::string(appdir) +
            "/usr/share/icons/hicolor/256x256/apps/audio-share-gui.png";
    } else {
        tray_icon = "audio-share-gui";
    }

    indicator = app_indicator_new(
        "audio-share-gui",
        tray_icon.c_str(),
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator, GTK_MENU(tray_menu(args)));

    gtk_widget_show_all(GTK_WIDGET(main_window));
    gtk_main();
    return 0;
}

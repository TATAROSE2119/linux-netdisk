#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 全局变量
GtkWidget *window;
GtkWidget *username_entry;
GtkWidget *password_entry;
GtkWidget *file_list;
GtkWidget *status_label;
GtkWidget *progress_bar;

// 连接状态
typedef struct {
    char username[64];
    int is_connected;
    int socket_fd;
} ClientState;

ClientState client_state = {0};

// 登录回调函数
void on_login_clicked(GtkWidget *widget, gpointer data) {
    const char *username = gtk_entry_get_text(GTK_ENTRY(username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(password_entry));
    
    if (strlen(username) == 0 || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(status_label), "请输入用户名和密码");
        return;
    }
    
    // TODO: 实现实际的登录逻辑
    // connect_to_server() 和 login_user() 函数
    
    gtk_label_set_text(GTK_LABEL(status_label), "正在登录...");
    
    // 模拟登录成功
    strcpy(client_state.username, username);
    client_state.is_connected = 1;
    
    gtk_label_set_text(GTK_LABEL(status_label), "登录成功！");
    
    // 刷新文件列表
    // refresh_file_list();
}

// 上传文件回调函数
void on_upload_clicked(GtkWidget *widget, gpointer data) {
    if (!client_state.is_connected) {
        gtk_label_set_text(GTK_LABEL(status_label), "请先登录");
        return;
    }
    
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "选择要上传的文件",
        GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_取消", GTK_RESPONSE_CANCEL,
        "_上传", GTK_RESPONSE_ACCEPT,
        NULL
    );
    
    gint res = gtk_dialog_run(GTK_DIALOG(dialog));
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        gtk_label_set_text(GTK_LABEL(status_label), "正在上传文件...");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
        
        // TODO: 实现实际的文件上传逻辑
        // upload_file(filename);
        
        // 模拟上传进度
        for (double i = 0.0; i <= 1.0; i += 0.1) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), i);
            while (gtk_events_pending()) {
                gtk_main_iteration();
            }
            g_usleep(100000); // 100ms延迟
        }
        
        gtk_label_set_text(GTK_LABEL(status_label), "文件上传完成");
        g_free(filename);
    }
    
    gtk_widget_destroy(dialog);
}

// 下载文件回调函数
void on_download_clicked(GtkWidget *widget, gpointer data) {
    if (!client_state.is_connected) {
        gtk_label_set_text(GTK_LABEL(status_label), "请先登录");
        return;
    }
    
    // TODO: 获取选中的文件并下载
    gtk_label_set_text(GTK_LABEL(status_label), "下载功能待实现");
}

// 刷新文件列表
void refresh_file_list() {
    // TODO: 从服务器获取文件列表并更新界面
}

// 创建主界面
void create_main_window() {
    // 创建主窗口
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "网盘客户端");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    
    // 创建主容器
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // 登录区域
    GtkWidget *login_frame = gtk_frame_new("登录");
    gtk_box_pack_start(GTK_BOX(main_box), login_frame, FALSE, FALSE, 0);
    
    GtkWidget *login_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(login_frame), login_box);
    gtk_container_set_border_width(GTK_CONTAINER(login_box), 10);
    
    // 用户名输入
    gtk_box_pack_start(GTK_BOX(login_box), gtk_label_new("用户名:"), FALSE, FALSE, 0);
    username_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(login_box), username_entry, TRUE, TRUE, 0);
    
    // 密码输入
    gtk_box_pack_start(GTK_BOX(login_box), gtk_label_new("密码:"), FALSE, FALSE, 0);
    password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(login_box), password_entry, TRUE, TRUE, 0);
    
    // 登录按钮
    GtkWidget *login_button = gtk_button_new_with_label("登录");
    g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(login_box), login_button, FALSE, FALSE, 0);
    
    // 操作按钮区域
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);
    
    GtkWidget *upload_button = gtk_button_new_with_label("上传文件");
    g_signal_connect(upload_button, "clicked", G_CALLBACK(on_upload_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), upload_button, TRUE, TRUE, 0);
    
    GtkWidget *download_button = gtk_button_new_with_label("下载文件");
    g_signal_connect(download_button, "clicked", G_CALLBACK(on_download_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), download_button, TRUE, TRUE, 0);
    
    GtkWidget *refresh_button = gtk_button_new_with_label("刷新列表");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_file_list), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), refresh_button, TRUE, TRUE, 0);
    
    // 文件列表区域
    GtkWidget *file_frame = gtk_frame_new("文件列表");
    gtk_box_pack_start(GTK_BOX(main_box), file_frame, TRUE, TRUE, 0);
    
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(file_frame), scrolled_window);
    
    file_list = gtk_tree_view_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), file_list);
    
    // 创建文件列表的列
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        "文件名", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list), column);
    
    column = gtk_tree_view_column_new_with_attributes(
        "大小", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list), column);
    
    column = gtk_tree_view_column_new_with_attributes(
        "修改时间", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(file_list), column);
    
    // 状态栏
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(main_box), status_box, FALSE, FALSE, 0);
    
    status_label = gtk_label_new("就绪");
    gtk_box_pack_start(GTK_BOX(status_box), status_label, FALSE, FALSE, 0);
    
    progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(status_box), progress_bar, FALSE, FALSE, 0);
    
    // 连接窗口关闭信号
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // 显示所有组件
    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    create_main_window();
    
    gtk_main();
    
    return 0;
}

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QTimer>

extern "C" {
    // 包含你的C语言网络函数
    // #include "client_network.h"
}

class NetDiskClient : public QMainWindow {
    Q_OBJECT

private:
    // UI组件
    QWidget *centralWidget;
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QPushButton *loginButton;
    QPushButton *uploadButton;
    QPushButton *downloadButton;
    QPushButton *deleteButton;
    QPushButton *refreshButton;
    QTreeWidget *fileTree;
    QProgressBar *progressBar;
    QLabel *statusLabel;
    
    // 客户端状态
    QString currentUser;
    bool isConnected;
    int socketFd;

public:
    NetDiskClient(QWidget *parent = nullptr) : QMainWindow(parent) {
        setupUI();
        connectSignals();
        isConnected = false;
        socketFd = -1;
    }

private slots:
    void onLoginClicked() {
        QString username = usernameEdit->text();
        QString password = passwordEdit->text();
        
        if (username.isEmpty() || password.isEmpty()) {
            QMessageBox::warning(this, "警告", "请输入用户名和密码");
            return;
        }
        
        statusLabel->setText("正在登录...");
        
        // TODO: 调用C语言的登录函数
        // int result = login_to_server(username.toUtf8().data(), password.toUtf8().data());
        
        // 模拟登录
        currentUser = username;
        isConnected = true;
        
        statusLabel->setText("登录成功");
        refreshFileList();
        
        // 更新UI状态
        loginButton->setText("注销");
        uploadButton->setEnabled(true);
        downloadButton->setEnabled(true);
        deleteButton->setEnabled(true);
        refreshButton->setEnabled(true);
    }
    
    void onUploadClicked() {
        if (!isConnected) {
            QMessageBox::warning(this, "警告", "请先登录");
            return;
        }
        
        QString fileName = QFileDialog::getOpenFileName(
            this, "选择要上传的文件", "", "所有文件 (*.*)");
        
        if (!fileName.isEmpty()) {
            statusLabel->setText("正在上传文件...");
            progressBar->setValue(0);
            
            // TODO: 调用C语言的上传函数
            // upload_file(fileName.toUtf8().data());
            
            // 模拟上传进度
            QTimer *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, [this, timer]() {
                static int progress = 0;
                progress += 10;
                progressBar->setValue(progress);
                if (progress >= 100) {
                    timer->stop();
                    timer->deleteLater();
                    statusLabel->setText("文件上传完成");
                    refreshFileList();
                    progress = 0;
                }
            });
            timer->start(100);
        }
    }
    
    void onDownloadClicked() {
        if (!isConnected) {
            QMessageBox::warning(this, "警告", "请先登录");
            return;
        }
        
        QTreeWidgetItem *item = fileTree->currentItem();
        if (!item) {
            QMessageBox::warning(this, "警告", "请选择要下载的文件");
            return;
        }
        
        QString fileName = item->text(0);
        QString savePath = QFileDialog::getSaveFileName(
            this, "保存文件", fileName, "所有文件 (*.*)");
        
        if (!savePath.isEmpty()) {
            statusLabel->setText("正在下载文件...");
            progressBar->setValue(0);
            
            // TODO: 调用C语言的下载函数
            // download_file(fileName.toUtf8().data(), savePath.toUtf8().data());
            
            // 模拟下载进度
            QTimer *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, [this, timer]() {
                static int progress = 0;
                progress += 15;
                progressBar->setValue(progress);
                if (progress >= 100) {
                    timer->stop();
                    timer->deleteLater();
                    statusLabel->setText("文件下载完成");
                    progress = 0;
                }
            });
            timer->start(80);
        }
    }
    
    void onDeleteClicked() {
        if (!isConnected) {
            QMessageBox::warning(this, "警告", "请先登录");
            return;
        }
        
        QTreeWidgetItem *item = fileTree->currentItem();
        if (!item) {
            QMessageBox::warning(this, "警告", "请选择要删除的文件");
            return;
        }
        
        QString fileName = item->text(0);
        int ret = QMessageBox::question(this, "确认删除", 
            QString("确定要删除文件 '%1' 吗？").arg(fileName));
        
        if (ret == QMessageBox::Yes) {
            // TODO: 调用C语言的删除函数
            // delete_file(fileName.toUtf8().data());
            
            statusLabel->setText("文件删除成功");
            refreshFileList();
        }
    }
    
    void onRefreshClicked() {
        refreshFileList();
    }

private:
    void setupUI() {
        centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
        
        // 登录区域
        QGroupBox *loginGroup = new QGroupBox("登录信息");
        QGridLayout *loginLayout = new QGridLayout(loginGroup);
        
        loginLayout->addWidget(new QLabel("用户名:"), 0, 0);
        usernameEdit = new QLineEdit();
        loginLayout->addWidget(usernameEdit, 0, 1);
        
        loginLayout->addWidget(new QLabel("密码:"), 1, 0);
        passwordEdit = new QLineEdit();
        passwordEdit->setEchoMode(QLineEdit::Password);
        loginLayout->addWidget(passwordEdit, 1, 1);
        
        loginButton = new QPushButton("登录");
        loginLayout->addWidget(loginButton, 0, 2, 2, 1);
        
        mainLayout->addWidget(loginGroup);
        
        // 操作按钮区域
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        
        uploadButton = new QPushButton("上传文件");
        uploadButton->setEnabled(false);
        buttonLayout->addWidget(uploadButton);
        
        downloadButton = new QPushButton("下载文件");
        downloadButton->setEnabled(false);
        buttonLayout->addWidget(downloadButton);
        
        deleteButton = new QPushButton("删除文件");
        deleteButton->setEnabled(false);
        buttonLayout->addWidget(deleteButton);
        
        refreshButton = new QPushButton("刷新列表");
        refreshButton->setEnabled(false);
        buttonLayout->addWidget(refreshButton);
        
        mainLayout->addLayout(buttonLayout);
        
        // 文件列表
        QGroupBox *fileGroup = new QGroupBox("文件列表");
        QVBoxLayout *fileLayout = new QVBoxLayout(fileGroup);
        
        fileTree = new QTreeWidget();
        fileTree->setHeaderLabels(QStringList() << "文件名" << "大小" << "修改时间");
        fileTree->header()->setStretchLastSection(true);
        fileLayout->addWidget(fileTree);
        
        mainLayout->addWidget(fileGroup);
        
        // 状态栏
        progressBar = new QProgressBar();
        progressBar->setVisible(false);
        
        statusLabel = new QLabel("就绪");
        statusBar()->addWidget(statusLabel, 1);
        statusBar()->addPermanentWidget(progressBar);
        
        // 设置窗口属性
        setWindowTitle("网盘客户端");
        resize(800, 600);
    }
    
    void connectSignals() {
        connect(loginButton, &QPushButton::clicked, this, &NetDiskClient::onLoginClicked);
        connect(uploadButton, &QPushButton::clicked, this, &NetDiskClient::onUploadClicked);
        connect(downloadButton, &QPushButton::clicked, this, &NetDiskClient::onDownloadClicked);
        connect(deleteButton, &QPushButton::clicked, this, &NetDiskClient::onDeleteClicked);
        connect(refreshButton, &QPushButton::clicked, this, &NetDiskClient::onRefreshClicked);
    }
    
    void refreshFileList() {
        if (!isConnected) return;
        
        fileTree->clear();
        statusLabel->setText("正在刷新文件列表...");
        
        // TODO: 调用C语言函数获取文件列表
        // FileInfo *files = get_file_list();
        
        // 模拟添加文件
        QTreeWidgetItem *item1 = new QTreeWidgetItem(fileTree);
        item1->setText(0, "document.txt");
        item1->setText(1, "1.2 KB");
        item1->setText(2, "2024-01-15 10:30");
        
        QTreeWidgetItem *item2 = new QTreeWidgetItem(fileTree);
        item2->setText(0, "photo.jpg");
        item2->setText(1, "2.5 MB");
        item2->setText(2, "2024-01-14 15:45");
        
        statusLabel->setText("文件列表刷新完成");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    NetDiskClient window;
    window.show();
    
    return app.exec();
}

#include "qt_client_demo.moc"

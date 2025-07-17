# 编译器和编译选项
CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lsqlite3

# 目标文件
SERVER = server/server
CLIENT = client/client
SERVER_OBJS = server/main.o
CLIENT_OBJS = client/main.o

# 默认目标
all: $(SERVER) $(CLIENT)

# 服务器编译
$(SERVER): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 客户端编译
$(CLIENT): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 对象文件编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 调试版本
debug: CFLAGS += -g -DDEBUG
debug: all

# 清理目标
clean:
	rm -f $(SERVER) $(CLIENT) $(SERVER_OBJS) $(CLIENT_OBJS)
	rm -f server/server_recv.txt client/*.txt *.txt

# 完全清理（包括数据库和用户文件）
distclean: clean
	rm -f netdisk.db
	rm -rf netdisk_data

# 安装（可选）
install: all
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(SERVER) $(DESTDIR)/usr/local/bin/
	cp $(CLIENT) $(DESTDIR)/usr/local/bin/

# 创建必要的目录
init:
	mkdir -p netdisk_data

.PHONY: all clean distclean install init debug

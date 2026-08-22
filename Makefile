# 编译器和编译选项
CC = gcc
CPPFLAGS = -Icommon/include -Iserver/include -Iclient/include -I/opt/homebrew/include
CFLAGS = -Wall -O2
SERVER_LDFLAGS = -L/opt/homebrew/lib -lsqlite3 -lssl -lcrypto -lpthread
CLIENT_LDFLAGS = -L/opt/homebrew/lib -lreadline

# 目标文件
SERVER = server/server
CLIENT = client/client
SERVER_SRCS = \
	server/src/main.c \
	server/src/thread_pool.c
CLIENT_SRCS = \
	client/src/main.c
COMMON_SRCS = \
	common/src/net_io.c
SERVER_OBJS = $(patsubst server/src/%.c,build/server/%.o,$(SERVER_SRCS))
CLIENT_OBJS = $(patsubst client/src/%.c,build/client/%.o,$(CLIENT_SRCS))
COMMON_OBJS = $(patsubst common/src/%.c,build/common/%.o,$(COMMON_SRCS))
ALL_OBJS=$(sort $(SERVER_OBJS) $(CLIENT_OBJS) $(COMMON_OBJS))
DEPS = $(ALL_OBJS:.o=.d)
# 默认目标
all: $(SERVER) $(CLIENT)

# 服务器编译
$(SERVER): $(SERVER_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(SERVER_LDFLAGS)

# 客户端编译
$(CLIENT): $(CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIENT_LDFLAGS)

# 对象文件编译规则
build/server/%.o: server/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

build/client/%.o: client/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

build/common/%.o: common/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

# 调试版本
debug: CFLAGS += -g -DDEBUG
debug: all

# 清理目标
clean:
	rm -rf build
	rm -f $(SERVER) $(CLIENT)

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

.PHONY: all clean distclean install init debug test-server

test-server: $(SERVER)
	python3 -m unittest discover -s server/tests -v

-include $(DEPS)

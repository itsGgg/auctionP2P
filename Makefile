# Nom de l'exécutable
TARGET = AP2P

# Répertoires
SRC_DIR = src
LIB_DIR = lib
BUILD_DIR = build

# Compilateur et options
CC = clang # Utiliser clang si on veut détecter + de warnings, sinon gcc
CFLAGS = -Wall -g

# Détection des OS
UNAME_S := $(shell uname -s)

# Détection OpenSSL + Readline sur macOS
ifeq ($(UNAME_S),Darwin)
	OPENSSL_DIR := $(shell brew --prefix openssl@3 2>/dev/null)
	READLINE_DIR := $(shell brew --prefix readline 2>/dev/null)

	ifneq ($(OPENSSL_DIR),)
		CFLAGS += -I$(OPENSSL_DIR)/include
		LDFLAGS += -L$(OPENSSL_DIR)/lib
	endif

	ifneq ($(READLINE_DIR),)
		CFLAGS += -I$(READLINE_DIR)/include
		LDFLAGS += -L$(READLINE_DIR)/lib
	endif
endif

LIBS = -lreadline -lssl -lcrypto

# Sources et objets
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS)))

# Par défaut, définir LOG_LEVEL à DEBUG, et LOG_TRIGGER à LOG_UNIQ
LOG_LEVEL ?= NO_LOG
LOG_TRIGGER ?= LOG_UNIQ

# Règle par défaut
all: $(TARGET)

# Génération de l'exécutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

# Compilation des fichiers .c en .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(LIB_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Création du répertoire build/ si nécessaire
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Mode débogage NOLOG & UNIQ
debug: CFLAGS += -DDEBUG
debug: CFLAGS += -DLOG_LEVEL=$(LOG_LEVEL)
debug: CFLAGS += -DLOG_TRIGGER=$(LOG_TRIGGER)
debug: re

# Mode INFO & UNIQ
info: LOG_LEVEL = LOG_INFO
info: debug

# Mode WARNING & UNIQ
warning: LOG_LEVEL = LOG_WARNING
warning: debug

# Mode ERROR & UNIQ
error: LOG_LEVEL = LOG_ERROR
error: debug

# Mode débogage NOLOG & MAX
max_debug : LOG_TRIGGER = LOG_MAX
max_debug : debug

# Mode INFO & MAX
max_info: LOG_TRIGGER = LOG_MAX
max_info: info

# Mode WARNING & MAX
max_warning: LOG_TRIGGER = LOG_MAX
max_warning: warning

# Mode ERROR & MAX
max_error: LOG_TRIGGER = LOG_MAX
max_error: error

# Nettoyage complet
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Reconstruction complète
re: clean all

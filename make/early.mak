CFLAGS := -std=gnu11 -Wall -Wextra -Wpedantic -Werror -Wno-gnu-zero-variadic-macro-arguments -Wno-unknown-warning-option -Wno-variadic-macro-arguments-omitted
LDFLAGS := -lm

ROOT_DIR != pwd
ROOT_DIR_NAME != basename $(ROOT_DIR)

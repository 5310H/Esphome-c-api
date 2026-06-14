CC = gcc
TIMESTAMP := $(shell TZ="America/New_York" date +"%Y-%m-%d %H:%M EST")
MD_FILES := README.md CONTRIBUTING.md ROADMAP.md

CFLAGS = -Wall -Wextra -O2 -Isrc/proto -Isrc -I/usr/local/include

# Standard system paths for the installed library
LDFLAGS = -L/usr/local/lib -Wl,-rpath,/usr/local/lib

# The library produced by building rweather/noise-c from source is named 'libnoise'
LIBS = -lnoise -lsodium -lm -lpthread

# NanoPB Generator Config (Adjust path to your nanopb installation)
NANOPB_GEN = nanopb_generator

# NanoPB and API Source Files
PROTO_DIR = src/proto
SRC_DIR = src

# Protocol definition files
PROTO_FILES = $(PROTO_DIR)/api.proto \
              $(PROTO_DIR)/api_options.proto

# Generated files
PROTO_SRCS = $(PROTO_FILES:.proto=.pb.c)
PROTO_HDRS = $(PROTO_FILES:.proto=.pb.h)

# Nanopb core implementation (assumed to be in PROTO_DIR)
NANOPB_CORE = $(PROTO_DIR)/pb_common.c \
              $(PROTO_DIR)/pb_encode.c \
              $(PROTO_DIR)/pb_decode.c

PROTO_OBJS = $(PROTO_SRCS:.c=.o) $(NANOPB_CORE:.c=.o)
CLIENT_MAIN_OBJ = $(SRC_DIR)/main.o
FAKE_MAIN_OBJ = $(SRC_DIR)/fake_device.o
TARGET = esphome_client
FAKE_DEVICE = fake_esphome_device

CLIENT_OBJS = $(CLIENT_MAIN_OBJ) $(PROTO_OBJS)
FAKE_OBJS = $(FAKE_MAIN_OBJ) $(PROTO_OBJS)

all: $(TARGET) $(FAKE_DEVICE)

$(TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $(CLIENT_OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

$(FAKE_DEVICE): $(FAKE_OBJS)
	$(CC) $(CFLAGS) $(FAKE_OBJS) -o $(FAKE_DEVICE) $(LDFLAGS) $(LIBS)

# Rule to generate Nanopb files from .proto files
protos: $(PROTO_SRCS) $(PROTO_HDRS) # Ensure both .c and .h are targets for generation

# Catch missing .proto files and provide a clear instruction
$(PROTO_DIR)/%.proto:
	$(error Protocol definition file $@ is missing. Please ensure the ESPHome .proto files are placed in $(PROTO_DIR))

$(PROTO_DIR)/%.pb.c $(PROTO_DIR)/%.pb.h: $(PROTO_DIR)/%.proto
	@mkdir -p $(PROTO_DIR)
	@echo "Generating Nanopb files from $<..."
	@$(NANOPB_GEN) --output-dir=$(PROTO_DIR) -I $(PROTO_DIR) $< || \
		(echo "Error: nanopb_generator failed. Ensure 'nanopb' is installed in your pythonenv: pip install nanopb"; exit 1)

update_metadata:
	@echo "Updating timestamps to $(TIMESTAMP)..."
	@sed -i 's/\*Last Updated: .* .*\*/\*Last Updated: $(TIMESTAMP)\*/' $(MD_FILES)

%.o: %.c $(PROTO_HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o src/*.o $(PROTO_DIR)/*.o $(PROTO_DIR)/*.pb.c $(PROTO_DIR)/*.pb.h $(TARGET) $(FAKE_DEVICE)

check-env:
	@command -v python3 >/dev/null 2>&1 || { echo >&2 "python3 is required but not installed."; exit 1; }
	@nanopb_generator --version >/dev/null 2>&1 || { echo >&2 "nanopb generator is missing. Run: pip install nanopb"; exit 1; }
	@echo "Environment check passed."

.PHONY: all clean update_metadata protos check-env
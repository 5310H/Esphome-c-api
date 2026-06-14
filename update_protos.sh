#!/usr/bin/env bash
set -e

echo "=== ESPHome API Proto Updater ==="

PROTO_DIR="src/proto"
mkdir -p "$PROTO_DIR"

echo "[1/3] Downloading .proto files..."
# These are the specific split files expected by the Makefile and main.c
BASE_URL="https://raw.githubusercontent.com/esphome/esphome/dev/esphome/components/api"

echo "Downloading consolidated api.proto and api_options.proto..."
curl -s -L "$BASE_URL/api.proto" -o "$PROTO_DIR/api.proto"
curl -s -L "$BASE_URL/api_options.proto" -o "$PROTO_DIR/api_options.proto"

# Remove package declaration to match the simplified naming in main.c
sed -i '/^package /d' "$PROTO_DIR/api.proto"
sed -i '/^package /d' "$PROTO_DIR/api_options.proto"

# Fix C keyword conflict by renaming 'void' message type to 'ApiVoid' everywhere
sed -i 's/\bvoid\b/ApiVoid/g' "$PROTO_DIR/api.proto"
sed -i 's/\bvoid\b/ApiVoid/g' "$PROTO_DIR/api_options.proto"
sed -i 's/\blog\b/ApiLog/g' "$PROTO_DIR/api.proto"
sed -i 's/\blog\b/ApiLog/g' "$PROTO_DIR/api_options.proto"

# Ensure EncryptedMessage is present in api.proto, as it's crucial for the client/fake_device
if ! grep -q "message EncryptedMessage" "$PROTO_DIR/api.proto"; then
    echo "Warning: EncryptedMessage not found in api.proto. Appending definition."
    cat <<EOF >> "$PROTO_DIR/api.proto"
message EncryptedMessage {
  uint32 type = 1;
  bytes data = 2;
}
EOF
fi

echo "[2/3] Ensuring Nanopb core is present..."
# The Makefile expects pb_common, pb_encode, and pb_decode in src/proto
NANOPB_URL="https://raw.githubusercontent.com/nanopb/nanopb/master"
for f in pb.h pb_common.h pb_common.c pb_encode.h pb_encode.c pb_decode.h pb_decode.c; do
    echo "Updating $f..."
    curl -s -L "$NANOPB_URL/$f" -o "$PROTO_DIR/$f"
done

echo "[3/3] Generating Nanopb C code..."
# Use the generator defined in the Makefile environment
nanopb_generator --verbose --output-dir="$PROTO_DIR" -I "$PROTO_DIR" \
    "$PROTO_DIR/api.proto" \
    "$PROTO_DIR/api_options.proto" || \
    (echo "Protobuf generation failed. Check if 'nanopb' is installed in your pythonenv."; exit 1)

echo "=== DONE ==="

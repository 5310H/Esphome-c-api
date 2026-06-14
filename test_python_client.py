import socket
import base64
from noise.connection import NoiseConnection
from noise.state import SymmetricState

def hex_dump(label, data):
    print(f"{label} ({len(data)} bytes):")
    hex_str = data.hex()
    for i in range(0, len(hex_str), 32):
        chunk = hex_str[i:i+32]
        spaced = ' '.join(chunk[j:j+2] for j in range(0, len(chunk), 2))
        print(f"  {spaced}")

def main():
    # Setup connection
    host = '192.168.69.136'
    port = 6053
    psk_base64 = '93lQ8xeyF152/qDFEOngsSIJ74BrNRJOvXPVs8yNOIQ='
    psk = base64.b64decode(psk_base64)
    
    print(f"Connecting to ESPHome device {host}:{port}...")
    
    # Initialize Noise Protocol (ESPHome specific format)
    proto = NoiseConnection.from_name(b'Noise_NNpsk0_25519_ChaChaPoly_SHA256')
    proto.set_as_initiator()
    proto.set_psks(psk)
    proto.set_prologue(b'NoiseAPIInit\x00\x00')
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)
    sock.connect((host, port))
    print('TCP Connected!\n')
    
    # 1. Send Client Hello (Cleartext)
    sock.sendall(b'\x01\x00\x00')
    print('-> Sent Client Hello (0x01 0x00 0x00)')
    
    # 2. Read Server Hello (Cleartext)
    hdr = sock.recv(3)
    if len(hdr) == 3 and hdr[0] == 1:
        length = (hdr[1] << 8) | hdr[2]
        payload = sock.recv(length)
        server_name = payload[1:payload.find(b'\x00', 1)].decode('utf-8')
        print(f'<- Received Server Hello (Device name: {server_name})')
        hex_dump('Server Hello Payload', payload)
    print()
    
    # 3. Start Handshake
    proto.start_handshake()
    
    # write_message() generates the -> e payload (e.pub + empty payload MAC)
    msg = proto.write_message()
    
    print('--- Internal Noise State ---')
    hex_dump('e.pub (Little-Endian)', msg[:32])
    hex_dump('MAC (Over empty payload)', msg[32:])
    hex_dump('h (Hash)', proto.noise_protocol.symmetric_state.h)
    hex_dump('ck (Chaining Key)', proto.noise_protocol.symmetric_state.ck)
    print('----------------------------\n')
    
    # 4. Send Noise Handshake Message with ESPHome Framing
    # ESPHome prepends a 0x00 error indicator byte to the payload
    frame_len = len(msg) + 1
    header = bytes([0x01, (frame_len >> 8) & 0xFF, frame_len & 0xFF])
    
    print('-> Sending Handshake Message (e.pub + MAC)')
    sock.sendall(header + b'\x00' + msg)
    
    # 5. Read Server Handshake Response
    res_hdr = sock.recv(3)
    if len(res_hdr) == 3 and res_hdr[0] == 1:
        res_len = (res_hdr[1] << 8) | res_hdr[2]
        res_payload = sock.recv(res_len)
        
        print('<- Received Handshake Response')
        hex_dump('Response Payload', res_payload)
        
        # ESPHome server response also starts with a 0x00 error indicator
        if res_payload[0] == 0x00:
            # Read the message into the noise protocol state machine
            proto.read_message(res_payload[1:])
            print('\n[SUCCESS] Noise Handshake Complete! Secure session established.')
            
            # The Noise protocol generates two cipher states for transport
            send_cipher, recv_cipher = proto.noise_protocol.cipher_state_handshake
            hex_dump('Transport Send Key', send_cipher.k)
            hex_dump('Transport Recv Key', recv_cipher.k)
        else:
            print('\n[ERROR] Server rejected handshake! Error payload:', res_payload)
            
if __name__ == '__main__':
    main()

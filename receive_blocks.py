#!/usr/bin/env python3
"""
Block Transfer Receiver - Saves blocks from Pico W SD card to GitHub repo
"""

import paho.mqtt.client as mqtt
import struct
import os
from datetime import datetime

class BlockReceiver:
    def __init__(self, broker="localhost", port=1883):
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.broker = broker
        self.port = port
        
        # Block state
        self.block_id = None
        self.total_parts = 0
        self.parts = {}
        self.start_time = None
        
    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"✅ Connected to {self.broker}:{self.port}")
            client.subscribe("pico/chunks")  # Blocks sent from SD card
            client.subscribe("pico/block")   # Notifications
            print("📡 Subscribed to pico/chunks and pico/block")
            print(f"💾 Blocks will be saved to: {os.getcwd()}/received/\n")
        else:
            print(f"❌ Connection failed: {rc}")
            exit(1)
    
    def on_message(self, client, userdata, msg):
        # Handle notifications
        if msg.topic == "pico/block" and len(msg.payload) < 8:
            try:
                notification = msg.payload.decode('utf-8', errors='ignore')
                if "BLOCK_COMPLETE" in notification or "BLOCK_RECEIVED" in notification:
                    print(f"📬 {notification}")
                    return
            except:
                pass
        
        if len(msg.payload) < 8:
            return
            
        try:
            # Parse header
            block_id, part_num, total_parts, data_len = struct.unpack('<HHHH', msg.payload[:8])
            chunk_data = msg.payload[8:8+data_len]
            
            if len(chunk_data) != data_len:
                return
            
            # New block
            if self.block_id != block_id:
                if self.block_id is not None:
                    print(f"⚠️  New block {block_id} started")
                self.block_id = block_id
                self.total_parts = total_parts
                self.parts = {}
                self.start_time = datetime.now()
                print(f"\n🆕 Receiving block {block_id}: {total_parts} parts")
            
            # Store chunk
            if part_num not in self.parts:
                self.parts[part_num] = chunk_data
                if len(self.parts) % 10 == 0 or len(self.parts) == total_parts:
                    print(f"📦 {len(self.parts)}/{total_parts} chunks")
            
            # Complete?
            if len(self.parts) == total_parts:
                self.save_block()
        except Exception as e:
            print(f"❌ Error: {e}")
    
    def save_block(self):
        try:
            # Check for missing chunks
            missing = [i for i in range(1, self.total_parts + 1) if i not in self.parts]
            if missing:
                print(f"❌ Missing chunks: {missing}")
                return
            
            # Reassemble
            image_data = bytearray()
            for i in range(1, self.total_parts + 1):
                image_data.extend(self.parts[i])
            
            if len(image_data) == 0:
                return
            
            # Detect file type
            ext = ".bin"
            if len(image_data) >= 2:
                if image_data[:2] == b'\xff\xd8':
                    ext = ".jpg"
                elif image_data[:2] == b'\x89\x50':
                    ext = ".png"
                elif image_data[:2] == b'\x47\x49':
                    ext = ".gif"
            
            # Create received directory
            current_dir = os.getcwd()
            received_dir = os.path.join(current_dir, "received")
            if not os.path.exists(received_dir):
                os.makedirs(received_dir)
                print(f"📁 Created directory: received/")
            
            # Generate filename
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"block_{self.block_id}_{timestamp}{ext}"
            filepath = os.path.join(received_dir, filename)
            
            # Save to repo
            with open(filepath, 'wb') as f:
                f.write(image_data)
            
            elapsed = (datetime.now() - self.start_time).total_seconds()
            print(f"💾 Saved: received/{filename} ({len(image_data)} bytes, {elapsed:.1f}s)\n")
            
            # Reset
            self.block_id = None
            self.parts = {}
        except Exception as e:
            print(f"❌ Error saving: {e}")
            import traceback
            traceback.print_exc()
    
    def start(self):
        try:
            print("="*60)
            print("📥 Block Transfer Receiver")
            print("="*60)
            print(f"📡 Connecting to {self.broker}:{self.port}...")
            print(f"💾 Saving to: {os.getcwd()}/received/\n")
            
            self.client.connect(self.broker, self.port, 60)
            print("⏳ Waiting for blocks from Pico W SD card...")
            print("   (Press Ctrl+C to stop)\n")
            self.client.loop_forever()
        except KeyboardInterrupt:
            print("\n👋 Stopped")
        except Exception as e:
            print(f"❌ Error: {e}")

if __name__ == "__main__":
    import sys
    broker = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 1883
    
    receiver = BlockReceiver(broker, port)
    receiver.start()


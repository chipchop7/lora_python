import meshtastic
import meshtastic.serial_interface

# メッセージ受信時に呼ばれる関数
def on_receive(packet, interface):
    # 受信したパケットのデータからテキストメッセージを抽出
    if packet["decoded"]:
        text = packet["decoded"].get("text", None)
        from_node = packet["from"]
        if text:
            print(f"[{from_node}] からのメッセージ: {text}")

# シリアルインターフェースで接続
iface = meshtastic.serial_interface.SerialInterface()

# コールバック登録（パケット受信時に on_receive() を呼ぶ）
iface.onReceive = on_receive

print("メッセージ受信待機中... Ctrl+C で終了")

# イベントループ的に動作させる（ずっと待つ）
try:
    import time
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("終了します。")
    iface.close()

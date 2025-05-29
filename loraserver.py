import socket
import threading
from collections import deque
import meshtastic
import meshtastic.serial_interface

HOST = '0.0.0.0'
PORT = 23

# Meshtastic 接続
iface = meshtastic.serial_interface.SerialInterface()
my_node = iface.myInfo.my_node_num
if not iface:
    print("Meshtasticデバイスに接続できませんでした")
    exit(1)

clients = []              # Telnetクライアント全体
zlog_clients = []  # 複数のZLOGクライアント
QSObool = True            # QSO送信中フラグ
qsoQ = deque()            # QSOキュー
qso = None                # 現在送信中のQSO

# QSOをキューに追加
def QSOqueue(qso_data):
    qsoQ.append(qso_data)
    print("QSOをキューに追加:", qso_data)

# QSOをキューから取り出す
def QSOPop():
    global QSObool, qsoQ, qso
    if QSObool:
        if qsoQ:
            qso = qsoQ.popleft()
            QSObool = False
            return qso
        else:
            print("QSOキューは空です")
            return None
    else:
        if qso:
          return qso

# LoRaへ送信
def send_lora_message(lora_message):
    if len(lora_message) < 2:
        print(f"LoRaメッセージが短すぎます")
        return
    iface.sendText(lora_message)

# ZLOGへQSOを送信
def send_zlog_QSO(zlog_QSO):
    global zlog_clients
    if zlog_clients:
        lora_data = zlog_QSO.decode('utf-8', errors='ignore').strip()
        for client in zlog_clients:
          try:
              client.sendall((lora_data + "\r\n").encode('utf-8'))
          except Exception as e:
              print("ZLOG送信失敗:", e)
              zlog_clients.remove(client)
    else:
        print("ZLOG接続がありません")

# LoRaから受信したときの処理
def on_receive(packet, interface):
    if packet["decoded"]:
        QSO_text = packet["decoded"].get("text", None)
        from_node = packet["from"]
        if QSO_text:
            send_zlog_QSO(QSO_text.encode('utf-8'))
            iface.sendText(f"{from_node}confirmation")

iface.onReceive = on_receive

# Telnetクライアントの処理
def handle_client(conn, addr):
    global zlog_conn, QSObool

    print(f"{addr} が接続しました")
    clients.append(conn)

    try:
        first_message = conn.recv(1024)
        id_str = first_message.decode('utf-8', errors='ignore').strip()

        if "ZLOG" in id_str.upper():
            zlog_clients.append(conn)
            print(f"{addr} をZLOGとして登録しました")

        while True:
            data = conn.recv(1024)
            if not data:
                print(f"{addr} が切断されました")
                break

            decoded = data.decode('utf-8', errors='ignore').strip()
            print(f"{addr} から受信: {decoded}")

            # ZLOGからのQSO
            if "PUTQSO" in decoded:
                QSOqueue(data)
                lora_text = QSOPop().decode('utf-8', errors='ignore').strip()
                send_lora_message(lora_text)

            # LoRaからの確認メッセージ
            elif f"{my_node}confirmation" in decoded:
                QSObool = True

            # QSOキューがまだ残っている場合
            elif qsoQ:
                lora_text = QSOPop().decode('utf-8', errors='ignore').strip()
                send_lora_message(lora_text)

            # 通常メッセージ
            else:
                print("受信:", decoded)

    except Exception as e:
        print(f"{addr} エラー:", e)

    finally:
        if conn in clients:
            clients.remove(conn)
        
        conn.close()

# Telnetサーバ起動
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen()
    print(f"Telnetサーバ起動中: {HOST}:{PORT}")

    while True:
        conn, addr = s.accept()
        thread = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
        thread.start()
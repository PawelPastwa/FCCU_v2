import queue
import struct
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
import can

# Identyfikatory CAN z hydrogreen-can-definitions.dbc (Extended 29-bit)
ID_TX_STATE           = 0x500
ID_TX_HYDROGEN        = 0x502
ID_TX_POWER           = 0x503
ID_TX_ENVIRONMENT     = 0x504
ID_TX_HYDROGEN_TOTAL  = 0x506

ID_CMD_ENABLE         = 0x510
ID_CMD_MAIN_VALVE     = 0x511
ID_CMD_MANUAL_PURGE   = 0x512
ID_CMD_FAN            = 0x513
ID_CMD_FAN_SETTINGS   = 0x520
ID_CMD_VALVE_SETTINGS = 0x521

STATE_NAMES = {
    0: "INIT",
    1: "STARTING",
    2: "RUNNING",
    3: "STOPPED",
}

FAN_MODES = {
    "0 - MANUAL": 0,
    "1 - LINEAR_TEMP": 1,
    "2 - PID": 2,
}


class FccuTesterApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("FCCU CAN Tester (29-bit Extended)")
        self.root.geometry("950x750")

        self.bus = None
        self.rx_running = False
        self.rx_thread = None
        self.ui_queue = queue.Queue()
        self.heartbeat_job = None

        self._build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(50, self._process_queue)

    # ---------------- UI ----------------
    def _build_ui(self):
        # 1. Połączenie CAN
        conn_frame = ttk.LabelFrame(self.root, text="Połączenie CAN", padding=8)
        conn_frame.pack(fill="x", padx=10, pady=5)

        ttk.Label(conn_frame, text="Interfejs:").grid(row=0, column=0, sticky="w")
        self.iface_var = tk.StringVar(value="slcan")
        ttk.Combobox(
            conn_frame,
            textvariable=self.iface_var,
            values=["slcan", "gs_usb", "pcan", "socketcan", "kvaser", "vector", "virtual"],
            width=12,
            state="readonly",
        ).grid(row=0, column=1, padx=5)

        ttk.Label(conn_frame, text="Kanał:").grid(row=0, column=2, sticky="w")
        self.channel_var = tk.StringVar(value="COM3")
        ttk.Entry(conn_frame, textvariable=self.channel_var, width=16).grid(row=0, column=3, padx=5)

        ttk.Label(conn_frame, text="Bitrate:").grid(row=0, column=4, sticky="w")
        self.bitrate_var = tk.StringVar(value="500000")
        ttk.Combobox(
            conn_frame,
            textvariable=self.bitrate_var,
            values=["125000", "250000", "500000", "1000000"],
            width=10,
        ).grid(row=0, column=5, padx=5)

        self.btn_connect = ttk.Button(conn_frame, text="Połącz", command=self._toggle_connect)
        self.btn_connect.grid(row=0, column=6, padx=10)

        self.conn_status_lbl = ttk.Label(conn_frame, text="Rozłączono", foreground="red")
        self.conn_status_lbl.grid(row=0, column=7, padx=5)

        # Główny kontener: lewa strona (sterowanie) | prawa strona (telemetria)
        middle = ttk.Frame(self.root)
        middle.pack(fill="both", expand=False, padx=10, pady=5)

        left_col = ttk.Frame(middle)
        left_col.pack(side="left", fill="both", expand=True, padx=(0, 5))

        right_col = ttk.Frame(middle)
        right_col.pack(side="right", fill="both", expand=True, padx=(5, 0))

        # 2. Sterowanie podstawowe
        ctrl_frame = ttk.LabelFrame(left_col, text="Komendy sterujące", padding=8)
        ctrl_frame.pack(fill="x", pady=(0, 5))

        self.hb_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            ctrl_frame,
            text="Auto Heartbeat (wysyłaj 0x510 = 0x01 co 1000 ms)",
            variable=self.hb_var,
            command=self._toggle_heartbeat,
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))

        ttk.Button(ctrl_frame, text="START / ENABLE (0x510#01)", command=self._cmd_enable).grid(
            row=1, column=0, sticky="ew", padx=3, pady=3
        )
        ttk.Button(ctrl_frame, text="STOP / FAIL-SAFE (0x510#00)", command=self._cmd_stop).grid(
            row=1, column=1, sticky="ew", padx=3, pady=3
        )

        ttk.Button(
            ctrl_frame,
            text="Zawór główny OPEN (0x511#01)",
            command=lambda: self._send_u8(ID_CMD_MAIN_VALVE, 1),
        ).grid(row=2, column=0, sticky="ew", padx=3, pady=3)
        ttk.Button(
            ctrl_frame,
            text="Zawór główny CLOSE (0x511#00)",
            command=lambda: self._send_u8(ID_CMD_MAIN_VALVE, 0),
        ).grid(row=2, column=1, sticky="ew", padx=3, pady=3)

        # Sterowanie wentylatorem z zadawaniem PWM 0..100% (0x513 - 2 bajty)
        fan_manual_row = ttk.Frame(ctrl_frame)
        fan_manual_row.grid(row=3, column=0, columnspan=2, sticky="ew", padx=3, pady=3)

        ttk.Label(fan_manual_row, text="PWM [%]:").pack(side="left", padx=(0, 4))
        self.fan_duty_var = tk.StringVar(value="30")
        ttk.Spinbox(
            fan_manual_row, from_=0, to=100, textvariable=self.fan_duty_var, width=5
        ).pack(side="left", padx=(0, 6))

        ttk.Button(fan_manual_row, text="Wentylator ON (0x513)", command=self._cmd_fan_on).pack(
            side="left", fill="x", expand=True, padx=(0, 3)
        )
        ttk.Button(fan_manual_row, text="Wentylator OFF", command=self._cmd_fan_off).pack(
            side="left", fill="x", expand=True, padx=(3, 0)
        )

        ttk.Button(
            ctrl_frame,
            text="Ręczny przedmuch PURGE (0x512#01)",
            command=lambda: self._send_u8(ID_CMD_MANUAL_PURGE, 1),
        ).grid(row=4, column=0, columnspan=2, sticky="ew", padx=3, pady=3)

        ctrl_frame.columnconfigure(0, weight=1)
        ctrl_frame.columnconfigure(1, weight=1)

        # 3. Ustawienia wentylatora (0x520)
        fan_frame = ttk.LabelFrame(left_col, text="Ustawienia wentylatora (0x520)", padding=8)
        fan_frame.pack(fill="x", pady=5)

        ttk.Label(fan_frame, text="Tryb:").grid(row=0, column=0, sticky="w")
        self.fan_mode_var = tk.StringVar(value="0 - MANUAL")
        ttk.Combobox(
            fan_frame,
            textvariable=self.fan_mode_var,
            values=list(FAN_MODES.keys()),
            width=18,
            state="readonly",
        ).grid(row=0, column=1, columnspan=3, sticky="w", padx=5, pady=2)

        ttk.Label(fan_frame, text="Kp:").grid(row=1, column=0, sticky="w")
        self.kp_var = tk.StringVar(value="10")
        ttk.Entry(fan_frame, textvariable=self.kp_var, width=7).grid(row=1, column=1, padx=3, pady=2)

        ttk.Label(fan_frame, text="Ki:").grid(row=1, column=2, sticky="w")
        self.ki_var = tk.StringVar(value="2")
        ttk.Entry(fan_frame, textvariable=self.ki_var, width=7).grid(row=1, column=3, padx=3, pady=2)

        ttk.Label(fan_frame, text="Kd:").grid(row=1, column=4, sticky="w")
        self.kd_var = tk.StringVar(value="1")
        ttk.Entry(fan_frame, textvariable=self.kd_var, width=7).grid(row=1, column=5, padx=3, pady=2)

        ttk.Button(fan_frame, text="Wyślij ustawienia wentylatora", command=self._send_fan_settings).grid(
            row=2, column=0, columnspan=6, sticky="ew", pady=(6, 0)
        )

        # 4. Ustawienia zaworu purge (0x521)
        valve_frame = ttk.LabelFrame(left_col, text="Ustawienia zaworu Purge (0x521)", padding=8)
        valve_frame.pack(fill="x", pady=(5, 0))

        ttk.Label(valve_frame, text="Interwał [s] (0=OFF):").grid(row=0, column=0, sticky="w")
        self.purge_int_var = tk.StringVar(value="0")
        ttk.Entry(valve_frame, textvariable=self.purge_int_var, width=8).grid(row=0, column=1, padx=5)

        ttk.Label(valve_frame, text="Czas [ms] (10-500):").grid(row=0, column=2, sticky="w")
        self.purge_dur_var = tk.StringVar(value="100")
        ttk.Entry(valve_frame, textvariable=self.purge_dur_var, width=8).grid(row=0, column=3, padx=5)

        ttk.Button(valve_frame, text="Wyślij ustawienia zaworu", command=self._send_valve_settings).grid(
            row=1, column=0, columnspan=2, sticky="ew", padx=3, pady=(6, 0)
        )
        ttk.Button(
            valve_frame,
            text="Test Anti-Idiot (1000 ms)",
            command=lambda: self._send_valve_raw(5, 1000),
        ).grid(row=1, column=2, columnspan=2, sticky="ew", padx=3, pady=(6, 0))

        # 5. Telemetria na żywo (prawa kolumna)
        tel_frame = ttk.LabelFrame(right_col, text="Telemetria z FCCU (RX)", padding=8)
        tel_frame.pack(fill="both", expand=True)

        self.tel_vars = {
            "state": tk.StringVar(value="---"),
            "main_valve": tk.StringVar(value="---"),
            "purge_valve": tk.StringVar(value="---"),
            "fan": tk.StringVar(value="---"),
            "h2_flow": tk.StringVar(value="---"),
            "h2_total": tk.StringVar(value="---"),
            "h2_lp": tk.StringVar(value="---"),
            "h2_hp": tk.StringVar(value="---"),
            "fc_power": tk.StringVar(value="---"),
            "env_temp": tk.StringVar(value="---"),
            "env_hum_press": tk.StringVar(value="---"),
            "last_rx": tk.StringVar(value="Brak ramek"),
        }

        rows = [
            ("Stan (0x500):", "state"),
            ("Zawór główny:", "main_valve"),
            ("Zawór purge:", "purge_valve"),
            ("Wentylator / PWM:", "fan"),
            ("Przepływ H2 (0x502):", "h2_flow"),
            ("Zużycie H2 całk. (0x506):", "h2_total"),
            ("Ciśnienie LP:", "h2_lp"),
            ("Ciśnienie HP:", "h2_hp"),
            ("Stos FC (0x503):", "fc_power"),
            ("Temperatura (0x504):", "env_temp"),
            ("Wilgotność / Ciśn.:", "env_hum_press"),
            ("Ostatnia ramka:", "last_rx"),
        ]

        for idx, (label_text, key) in enumerate(rows):
            ttk.Label(tel_frame, text=label_text, font=("Segoe UI", 9, "bold")).grid(
                row=idx, column=0, sticky="w", pady=3, padx=(0, 10)
            )
            ttk.Label(tel_frame, textvariable=self.tel_vars[key], font=("Consolas", 10)).grid(
                row=idx, column=1, sticky="w", pady=3
            )

        # 6. Log ramek
        log_frame = ttk.LabelFrame(self.root, text="Log ramek CAN", padding=6)
        log_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.log_text = tk.Text(log_frame, height=11, font=("Consolas", 9), state="disabled")
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scrollbar.set)

        self.log_text.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        ttk.Button(self.root, text="Wyczyść log", command=self._clear_log).pack(
            anchor="e", padx=10, pady=(0, 8)
        )

    # ---------------- CAN Connection ----------------
    def _toggle_connect(self):
        if self.bus is not None:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        iface = self.iface_var.get().strip()
        channel_raw = self.channel_var.get().strip()
        try:
            bitrate = int(self.bitrate_var.get().strip())
        except ValueError:
            messagebox.showerror("Błąd", "Nieprawidłowy bitrate.")
            return

        channel = int(channel_raw) if (iface == "gs_usb" and channel_raw.isdigit()) else channel_raw

        try:
            kwargs = {"interface": iface, "channel": channel}
            if iface != "virtual":
                kwargs["bitrate"] = bitrate
            self.bus = can.Bus(**kwargs)
        except Exception as e:
            messagebox.showerror("Błąd połączenia CAN", str(e))
            self.bus = None
            return

        self.rx_running = True
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()

        self.btn_connect.config(text="Rozłącz")
        self.conn_status_lbl.config(text=f"Połączono ({iface}:{channel})", foreground="green")
        self._append_log(f"--- Połączono: {iface}, kanał={channel}, bitrate={bitrate} ---")

    def _disconnect(self):
        self.hb_var.set(False)
        self._cancel_heartbeat()
        self.rx_running = False
        if self.bus is not None:
            try:
                self.bus.shutdown()
            except Exception:
                pass
            self.bus = None

        self.btn_connect.config(text="Połącz")
        self.conn_status_lbl.config(text="Rozłączono", foreground="red")
        self._append_log("--- Rozłączono ---")

    # ---------------- CAN TX ----------------
    def _send_frame(self, arb_id: int, data: bytes):
        if self.bus is None:
            messagebox.showwarning("Brak połączenia", "Najpierw połącz się z magistralą CAN.")
            return
        msg = can.Message(arbitration_id=arb_id, data=data, is_extended_id=True)
        try:
            self.bus.send(msg)
            self._append_log(f"TX | ID=0x{arb_id:08X} | DLC={len(data)} | {data.hex(' ').upper()}")
        except Exception as e:
            self._append_log(f"TX ERR | ID=0x{arb_id:08X} | {e}")

    def _send_u8(self, arb_id: int, val: int):
        self._send_frame(arb_id, bytes([val & 0xFF]))

    def _cmd_enable(self):
        self._send_u8(ID_CMD_ENABLE, 1)

    def _cmd_stop(self):
        self.hb_var.set(False)
        self._cancel_heartbeat()
        self._send_u8(ID_CMD_ENABLE, 0)

    def _cmd_fan_on(self):
        try:
            duty = int(self.fan_duty_var.get())
            if not (0 <= duty <= 100):
                raise ValueError
        except ValueError:
            messagebox.showerror("Błąd danych", "Wpisz wypełnienie PWM w zakresie 0..100%.")
            return
        self._send_frame(ID_CMD_FAN, bytes([1, duty]))

    def _cmd_fan_off(self):
        self._send_frame(ID_CMD_FAN, bytes([0, 0]))

    def _send_fan_settings(self):
        try:
            mode = FAN_MODES[self.fan_mode_var.get()]
            kp = int(self.kp_var.get())
            ki = int(self.ki_var.get())
            kd = int(self.kd_var.get())
            payload = struct.pack("<HHHH", mode, kp, ki, kd)
        except Exception as e:
            messagebox.showerror("Błąd danych", f"Sprawdź wartości P/I/D (0..1000):\n{e}")
            return
        self._send_frame(ID_CMD_FAN_SETTINGS, payload)

    def _send_valve_settings(self):
        try:
            interval_s = int(self.purge_int_var.get())
            duration_ms = int(self.purge_dur_var.get())
        except ValueError:
            messagebox.showerror("Błąd danych", "Wpisz liczby całkowite.")
            return
        self._send_valve_raw(interval_s, duration_ms)

    def _send_valve_raw(self, interval_s: int, duration_ms: int):
        try:
            payload = struct.pack("<hH", interval_s, duration_ms)
        except Exception as e:
            messagebox.showerror("Błąd danych", f"Zakres interwału -1..16383, czasu 0..65535:\n{e}")
            return
        self._send_frame(ID_CMD_VALVE_SETTINGS, payload)

    # ---------------- Heartbeat ----------------
    def _toggle_heartbeat(self):
        if self.hb_var.get():
            if self.bus is None:
                self.hb_var.set(False)
                messagebox.showwarning("Brak połączenia", "Najpierw połącz się z CAN.")
                return
            self._heartbeat_tick()
        else:
            self._cancel_heartbeat()

    def _heartbeat_tick(self):
        if self.hb_var.get() and self.bus is not None:
            self._send_u8(ID_CMD_ENABLE, 1)
            self.heartbeat_job = self.root.after(1000, self._heartbeat_tick)

    def _cancel_heartbeat(self):
        if self.heartbeat_job is not None:
            self.root.after_cancel(self.heartbeat_job)
            self.heartbeat_job = None

    # ---------------- CAN RX & Decode ----------------
    def _rx_loop(self):
        while self.rx_running and self.bus is not None:
            try:
                msg = self.bus.recv(timeout=0.1)
                if msg is not None:
                    self.ui_queue.put(msg)
            except Exception:
                if not self.rx_running:
                    break
                time.sleep(0.1)

    def _process_queue(self):
        try:
            while True:
                msg = self.ui_queue.get_nowait()
                self._handle_rx_msg(msg)
        except queue.Empty:
            pass
        self.root.after(50, self._process_queue)

    def _handle_rx_msg(self, msg: can.Message):
        data = bytes(msg.data)
        self._append_log(f"RX | ID=0x{msg.arbitration_id:08X} | DLC={len(data)} | {data.hex(' ').upper()}")
        self.tel_vars["last_rx"].set(time.strftime("%H:%M:%S"))

        if msg.arbitration_id == ID_TX_STATE and len(data) >= 2:
            b0, duty = data[0], data[1]
            state_code = b0 & 0x0F
            main_v = (b0 >> 4) & 1
            purge_v = (b0 >> 5) & 1
            fan_on = (b0 >> 6) & 1

            self.tel_vars["state"].set(f"{STATE_NAMES.get(state_code, 'UNKNOWN')} ({state_code})")
            self.tel_vars["main_valve"].set("OPEN" if main_v else "CLOSED")
            self.tel_vars["purge_valve"].set("PURGING" if purge_v else "CLOSED")
            self.tel_vars["fan"].set(f"{'ON' if fan_on else 'OFF'} ({duty}%)")

        elif msg.arbitration_id == ID_TX_HYDROGEN and len(data) >= 8:
            flow, lp, hp, _ = struct.unpack("<HHHh", data[:8])
            self.tel_vars["h2_flow"].set(f"{flow / 100.0:.2f} Ln/min")
            self.tel_vars["h2_lp"].set(f"{lp / 100.0:.2f} bar")
            self.tel_vars["h2_hp"].set(f"{hp / 100.0:.2f} bar")

        elif msg.arbitration_id == ID_TX_HYDROGEN_TOTAL and len(data) >= 4:
            (total_x100,) = struct.unpack("<I", data[:4])
            self.tel_vars["h2_total"].set(f"{total_x100 / 100.0:.2f} Ln")

        elif msg.arbitration_id == ID_TX_POWER and len(data) >= 8:
            fc_v, fc_i, _, _ = struct.unpack("<hhhh", data[:8])
            self.tel_vars["fc_power"].set(f"{fc_v / 100.0:.2f} V | {fc_i / 100.0:.2f} A")

        elif msg.arbitration_id == ID_TX_ENVIRONMENT and len(data) >= 8:
            fc_t, amb_t, hum, press = struct.unpack("<hhHH", data[:8])
            self.tel_vars["env_temp"].set(f"FC: {fc_t / 100.0:.2f} °C | Amb: {amb_t / 100.0:.2f} °C")
            self.tel_vars["env_hum_press"].set(f"{hum / 100.0:.2f} % | raw={press}")

    # ---------------- Helpers ----------------
    def _append_log(self, text: str):
        ts = time.strftime("%H:%M:%S")
        self.log_text.config(state="normal")
        self.log_text.insert("end", f"[{ts}] {text}\n")
        self.log_text.see("end")
        self.log_text.config(state="disabled")

    def _clear_log(self):
        self.log_text.config(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.config(state="disabled")

    def _on_close(self):
        self._disconnect()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = FccuTesterApp(root)
    root.mainloop()
#!/usr/bin/env python3
"""
FCCU v2 — Ostateczny tester CAN (ręczny + automatyczne testy PASS/FAIL)
Interfejs domyślny: PCAN_USBBUS1 @ 500 kbps (Extended CAN IDs).
Nie wymaga ładowania pliku .dbc — układ bajtów zgodny 1:1 z hydrogreen-can-definitions.dbc.
"""

import queue
import struct
import threading
import time
from collections import deque
import tkinter as tk
from tkinter import messagebox, ttk

import can


STATE_LABELS = {
    0: "INIT (0)",
    1: "STARTING (1)",
    2: "RUNNING (2)",
    3: "STOPPED (3)",
}

FAN_MODE_MAP = {
    "0 - MANUAL": 0,
    "1 - LINEAR": 1,
    "2 - PID": 2,
}

PURGE_MODE_MAP = {
    "0 - TIME": 0,
    "1 - VOLTAGE": 1,
    "2 - BOTH": 2,
}

FCCU_RX_IDS = {0x500, 0x501, 0x502, 0x503, 0x504, 0x505, 0x506, 0x507}

TEST_SPECS = [
    ("t1_boot",  "1. Auto-start INIT -> RUNNING (0x500)"),
    ("t2_flow",  "2. Przepływ 0x506 (1 Hz) + 0x507 (100 Hz)"),
    ("t3_temp",  "3. Termistor FC / środowisko (0x504)"),
    ("t4_fan",   "4. Wentylator 30% + STOPPED -> RUNNING"),
    ("t5_lock",  "5. Blokada auto-purge (MAIN_VALVE=0)"),
    ("t6_purge", "6. Auto-purge (MAIN_VALVE=1: V_FC + TIME)"),
    ("t7_flash", "7. Pamięć Flash po restarcie zasilania"),
]


class FCCUTesterApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("FCCU v2 — Ostateczny Tester CAN")
        self.root.geometry("1320x880")

        self.bus = None
        self.running = False
        self.rx_thread = None
        self.mcu_tx_thread = None

        self.auto_test_thread = None
        self.auto_test_stop = threading.Event()
        self.auto_test_running = False

        self.lock = threading.Lock()
        self.log_queue: queue.Queue[str] = queue.Queue(maxsize=1500)

        self.telemetry = {
            "state": None,
            "main_valve": None,
            "purge_valve": None,
            "fan_state": None,
            "fan_duty": None,
            "h2_lp": 0.0,
            "h2_hp": 0.0,
            "leakage_v": 0.0,
            "h2_total": 0.0,
            "h2_flow": 0.0,
            "fc_temp_c": 0.0,
            "amb_temp_c": 0.0,
            "amb_hum": 0.0,
            "amb_press": 0.0,
            "fc_v": 0.0,
            "fc_a": 0.0,
            "dcdc_v": 0.0,
            "dcdc_a": 0.0,
            "last_rx_500": None,
            "last_rx_504": None,
            "last_rx_506": None,
            "last_rx_507": None,
            "count_504": 0,
            "count_506": 0,
            "count_507": 0,
            "purge_count": 0,
            "last_purge_ts": None,
            "prev_purge_valve": 0,
        }
        self.rx_507_times: deque[float] = deque(maxlen=2500)

        self.test_status: dict[str, tuple[str, str]] = {
            key: ("OCZEKUJE", "#555555") for key, _ in TEST_SPECS
        }
        self.auto_progress_text = "Gotowy do testów"

        self._build_ui()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(100, self._refresh_ui)

    # ------------------------------------------------------------------
    # GUI
    # ------------------------------------------------------------------
    def _build_ui(self) -> None:
        top = ttk.LabelFrame(self.root, text="Połączenie CAN (Extended IDs, 500 kbps)", padding=6)
        top.pack(fill="x", padx=8, pady=4)

        ttk.Label(top, text="Interfejs:").grid(row=0, column=0, sticky="w")
        self.iface_var = tk.StringVar(value="pcan")
        ttk.Entry(top, textvariable=self.iface_var, width=10).grid(row=0, column=1, padx=4)

        ttk.Label(top, text="Kanał:").grid(row=0, column=2, sticky="w")
        self.channel_var = tk.StringVar(value="PCAN_USBBUS1")
        ttk.Entry(top, textvariable=self.channel_var, width=16).grid(row=0, column=3, padx=4)

        ttk.Label(top, text="Bitrate:").grid(row=0, column=4, sticky="w")
        self.bitrate_var = tk.StringVar(value="500000")
        ttk.Entry(top, textvariable=self.bitrate_var, width=10).grid(row=0, column=5, padx=4)

        self.connect_btn = ttk.Button(top, text="Połącz", command=self._toggle_connect)
        self.connect_btn.grid(row=0, column=6, padx=6)

        self.conn_status_var = tk.StringVar(value="Rozłączony")
        self.conn_status_lbl = ttk.Label(top, textvariable=self.conn_status_var, foreground="red")
        self.conn_status_lbl.grid(row=0, column=7, padx=6)

        mid = ttk.Frame(self.root)
        mid.pack(fill="both", expand=False, padx=8, pady=4)

        left = ttk.Frame(mid)
        left.pack(side="left", fill="both", expand=True, padx=(0, 6))

        right = ttk.Frame(mid)
        right.pack(side="right", fill="both", expand=True, padx=(6, 0))

        self._build_controls_column(left)
        self._build_right_column(right)

        # Log CAN
        log_frame = ttk.LabelFrame(self.root, text="Log CAN i raport testów", padding=6)
        log_frame.pack(fill="both", expand=True, padx=8, pady=4)

        filters = ttk.Frame(log_frame)
        filters.pack(fill="x", pady=(0, 4))

        self.log_507_var = tk.BooleanVar(value=False)
        self.log_104_tx_var = tk.BooleanVar(value=False)
        self.log_other_var = tk.BooleanVar(value=False)

        ttk.Checkbutton(filters, text="Loguj 0x507 (100 Hz)", variable=self.log_507_var).pack(side="left", padx=4)
        ttk.Checkbutton(filters, text="Loguj TX 0x104", variable=self.log_104_tx_var).pack(side="left", padx=4)
        ttk.Checkbutton(filters, text="Loguj obce ramki", variable=self.log_other_var).pack(side="left", padx=4)
        ttk.Button(filters, text="Reset liczników", command=self._reset_counters).pack(side="right", padx=4)
        ttk.Button(filters, text="Wyczyść log", command=self._clear_log).pack(side="right", padx=4)

        self.log_text = tk.Text(log_frame, height=11, font=("Consolas", 9), state="disabled")
        scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        self.log_text.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

    def _build_controls_column(self, parent: ttk.Frame) -> None:
        # 1. System i zawory
        sys_box = ttk.LabelFrame(parent, text="1. System i zawory (0x510, 0x511, 0x512)", padding=6)
        sys_box.pack(fill="x", pady=3)

        ttk.Button(sys_box, text="RUNNING (0x510=1)", command=lambda: self._send_system_enable(1)).grid(
            row=0, column=0, padx=3, pady=2, sticky="ew"
        )
        ttk.Button(sys_box, text="STOPPED (0x510=0)", command=lambda: self._send_system_enable(0)).grid(
            row=0, column=1, padx=3, pady=2, sticky="ew"
        )
        ttk.Button(sys_box, text="Main Valve OPEN (0x511=1)", command=lambda: self._send_main_valve(1)).grid(
            row=1, column=0, padx=3, pady=2, sticky="ew"
        )
        ttk.Button(sys_box, text="Main Valve CLOSE (0x511=0)", command=lambda: self._send_main_valve(0)).grid(
            row=1, column=1, padx=3, pady=2, sticky="ew"
        )
        ttk.Button(sys_box, text="Manual PURGE (0x512=1)", command=self._send_manual_purge).grid(
            row=2, column=0, columnspan=2, padx=3, pady=2, sticky="ew"
        )
        sys_box.columnconfigure(0, weight=1)
        sys_box.columnconfigure(1, weight=1)

        # 2. Wentylator + PID
        fan_box = ttk.LabelFrame(parent, text="2. Wentylator + ustawienia PID / Flash (0x513, 0x520)", padding=6)
        fan_box.pack(fill="x", pady=3)

        ttk.Label(fan_box, text="PWM [%]:").grid(row=0, column=0, sticky="w")
        self.fan_duty_var = tk.StringVar(value="30")
        ttk.Entry(fan_box, textvariable=self.fan_duty_var, width=8).grid(row=0, column=1, padx=4, sticky="w")
        ttk.Button(fan_box, text="Wentylator ON (0x513)", command=lambda: self._send_fan_cmd(1)).grid(
            row=0, column=2, padx=3, sticky="ew"
        )
        ttk.Button(fan_box, text="Wentylator OFF (0x513)", command=lambda: self._send_fan_cmd(0)).grid(
            row=0, column=3, padx=3, sticky="ew"
        )

        ttk.Label(fan_box, text="Tryb:").grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.fan_mode_var = tk.StringVar(value="0 - MANUAL")
        ttk.Combobox(
            fan_box, textvariable=self.fan_mode_var, values=list(FAN_MODE_MAP.keys()), state="readonly", width=13
        ).grid(row=1, column=1, padx=4, pady=(6, 0), sticky="w")

        pid_row = ttk.Frame(fan_box)
        pid_row.grid(row=1, column=2, columnspan=2, sticky="ew", pady=(6, 0))
        self.p_gain_var = tk.StringVar(value="10")
        self.i_gain_var = tk.StringVar(value="1")
        self.d_gain_var = tk.StringVar(value="0")
        for lbl, var in (("P:", self.p_gain_var), ("I:", self.i_gain_var), ("D:", self.d_gain_var)):
            ttk.Label(pid_row, text=lbl).pack(side="left")
            ttk.Entry(pid_row, textvariable=var, width=5).pack(side="left", padx=(1, 6))

        ttk.Button(fan_box, text="Wyślij ustawienia wentylatora (0x520)", command=self._send_fan_settings).grid(
            row=2, column=0, columnspan=4, pady=(6, 0), sticky="ew"
        )
        fan_box.columnconfigure(2, weight=1)
        fan_box.columnconfigure(3, weight=1)

        # 3. Ustawienia purge
        purge_box = ttk.LabelFrame(parent, text="3. Ustawienia Purge — działa gdy MAIN_VALVE=1 (0x521)", padding=6)
        purge_box.pack(fill="x", pady=3)

        ttk.Label(purge_box, text="Tryb purge:").grid(row=0, column=0, sticky="w")
        self.purge_mode_var = tk.StringVar(value="2 - BOTH")
        ttk.Combobox(
            purge_box, textvariable=self.purge_mode_var, values=list(PURGE_MODE_MAP.keys()), state="readonly", width=13
        ).grid(row=0, column=1, padx=4, sticky="w")

        ttk.Label(purge_box, text="Interwał [s]:").grid(row=0, column=2, sticky="w")
        self.purge_interval_var = tk.StringVar(value="10")
        ttk.Entry(purge_box, textvariable=self.purge_interval_var, width=8).grid(row=0, column=3, padx=4, sticky="w")

        ttk.Label(purge_box, text="Czas otwarcia [ms]:").grid(row=1, column=0, sticky="w", pady=(4, 0))
        self.purge_duration_var = tk.StringVar(value="100")
        ttk.Entry(purge_box, textvariable=self.purge_duration_var, width=8).grid(
            row=1, column=1, padx=4, pady=(4, 0), sticky="w"
        )

        ttk.Label(purge_box, text="Próg V_FC [mV]:").grid(row=1, column=2, sticky="w", pady=(4, 0))
        self.purge_v_thr_var = tk.StringVar(value="20000")
        ttk.Entry(purge_box, textvariable=self.purge_v_thr_var, width=8).grid(
            row=1, column=3, padx=4, pady=(4, 0), sticky="w"
        )

        ttk.Label(purge_box, text="Cooldown [s]:").grid(row=2, column=0, sticky="w", pady=(4, 0))
        self.purge_cooldown_var = tk.StringVar(value="5")
        ttk.Entry(purge_box, textvariable=self.purge_cooldown_var, width=8).grid(
            row=2, column=1, padx=4, pady=(4, 0), sticky="w"
        )

        ttk.Button(purge_box, text="Wyślij ustawienia purge (0x521)", command=self._send_valve_settings).grid(
            row=2, column=2, columnspan=2, padx=4, pady=(4, 0), sticky="ew"
        )
        purge_box.columnconfigure(3, weight=1)

        # 4. Symulator MCU 0x104
        mcu_box = ttk.LabelFrame(parent, text="4. Symulator MCU_ANALOG_FUEL_CELL (0x104 — test V_FC)", padding=6)
        mcu_box.pack(fill="x", pady=3)

        ttk.Label(mcu_box, text="Napięcie FC [V]:").grid(row=0, column=0, sticky="w")
        self.mcu_v_var = tk.StringVar(value="25.0")
        ttk.Entry(mcu_box, textvariable=self.mcu_v_var, width=8).grid(row=0, column=1, padx=4, sticky="w")

        self.mcu_sim_enable_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(mcu_box, text="Nadawaj co 100 ms", variable=self.mcu_sim_enable_var).grid(
            row=0, column=2, padx=4, sticky="w"
        )
        ttk.Button(mcu_box, text="Wyślij 0x104 raz", command=self._send_mcu_fc_voltage).grid(
            row=0, column=3, padx=4, sticky="ew"
        )

        presets = ttk.Frame(mcu_box)
        presets.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(5, 0))
        ttk.Button(presets, text="25.0 V (OK)", command=lambda: self.mcu_v_var.set("25.0")).pack(
            side="left", expand=True, fill="x", padx=2
        )
        ttk.Button(presets, text="18.0 V (<= próg -> PURGE)", command=lambda: self.mcu_v_var.set("18.0")).pack(
            side="left", expand=True, fill="x", padx=2
        )
        ttk.Button(presets, text="3.0 V (<5 V -> blokada)", command=lambda: self.mcu_v_var.set("3.0")).pack(
            side="left", expand=True, fill="x", padx=2
        )

    def _build_right_column(self, parent: ttk.Frame) -> None:
        self.tel_vars: dict[str, tk.StringVar] = {}

        def add_row(box: ttk.LabelFrame, row: int, label: str, key: str, default: str = "---", bold: bool = False) -> None:
            ttk.Label(box, text=label).grid(row=row, column=0, sticky="w", padx=4, pady=1)
            var = tk.StringVar(value=default)
            self.tel_vars[key] = var
            font = ("Consolas", 10, "bold") if bold else ("Consolas", 10)
            ttk.Label(box, textvariable=var, font=font).grid(row=row, column=1, sticky="e", padx=4, pady=1)

        b_tel = ttk.LabelFrame(parent, text="Telemetria na żywo (0x500, 0x502–0x507)", padding=6)
        b_tel.pack(fill="x", pady=3)
        b_tel.columnconfigure(1, weight=1)

        add_row(b_tel, 0, "Stan FCCU (0x500):", "state", bold=True)
        add_row(b_tel, 1, "Zawory MAIN / PURGE:", "valves", bold=True)
        add_row(b_tel, 2, "Licznik purge / ostatni:", "purge_stats")
        add_row(b_tel, 3, "Wentylator (STATE / DUTY):", "fan_status", bold=True)
        add_row(b_tel, 4, "Całkowite H2 (0x506):", "h2_total", bold=True)
        add_row(b_tel, 5, "Przepływ H2 (0x507):", "h2_flow", bold=True)
        add_row(b_tel, 6, "Częstotliwość RX 0x507:", "hz_507", bold=True)
        add_row(b_tel, 7, "Ciśnienia / Wyciek (0x502):", "flow_press")
        add_row(b_tel, 8, "Termistor FC — GPIO10 (0x504):", "fc_temp", bold=True)
        add_row(b_tel, 9, "BME280 T / RH / P (0x504):", "bme280")
        add_row(b_tel, 10, "Moc FC / DCDC (0x503):", "power")

        b_auto = ttk.LabelFrame(parent, text="Automatyczna weryfikacja wszystkich funkcji (PASS / FAIL)", padding=6)
        b_auto.pack(fill="both", expand=True, pady=3)
        b_auto.columnconfigure(1, weight=1)

        btn_row1 = ttk.Frame(b_auto)
        btn_row1.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        ttk.Button(btn_row1, text="Uruchom testy 1–6", command=self._start_auto_tests).pack(
            side="left", expand=True, fill="x", padx=2
        )
        ttk.Button(btn_row1, text="Przerwij", command=self._abort_auto_tests).pack(side="left", padx=2)

        btn_row2 = ttk.Frame(b_auto)
        btn_row2.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        ttk.Button(btn_row2, text="7a. Zapisz preset do Flash", command=self._save_flash_preset).pack(
            side="left", expand=True, fill="x", padx=2
        )
        ttk.Button(btn_row2, text="7b. Sprawdź po restarcie zasilania", command=self._verify_flash_after_reboot).pack(
            side="left", expand=True, fill="x", padx=2
        )

        self.auto_progress_var = tk.StringVar(value=self.auto_progress_text)
        ttk.Label(b_auto, textvariable=self.auto_progress_var, foreground="#004488").grid(
            row=2, column=0, columnspan=2, sticky="w", padx=4, pady=(0, 4)
        )

        self.test_lbls: dict[str, ttk.Label] = {}
        for idx, (key, title) in enumerate(TEST_SPECS, start=3):
            ttk.Label(b_auto, text=title).grid(row=idx, column=0, sticky="w", padx=4, pady=1)
            lbl = ttk.Label(b_auto, text="OCZEKUJE", font=("Consolas", 9, "bold"), foreground="#555555")
            lbl.grid(row=idx, column=1, sticky="e", padx=4, pady=1)
            self.test_lbls[key] = lbl

    # ------------------------------------------------------------------
    # Połączenie CAN
    # ------------------------------------------------------------------
    def _toggle_connect(self) -> None:
        if self.bus is None:
            self._connect()
        else:
            self._disconnect()

    def _connect(self) -> None:
        try:
            iface = self.iface_var.get().strip()
            channel = self.channel_var.get().strip()
            bitrate = int(self.bitrate_var.get().strip())

            self.bus = can.Bus(interface=iface, channel=channel, bitrate=bitrate)
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
            self.mcu_tx_thread = threading.Thread(target=self._mcu_sim_loop, daemon=True)
            self.mcu_tx_thread.start()

            self.connect_btn.configure(text="Rozłącz")
            self.conn_status_var.set(f"Połączono ({channel})")
            self.conn_status_lbl.configure(foreground="green")
            self._log(f"[CAN] Połączono: {iface} / {channel} @ {bitrate} bps")
        except Exception as exc:
            self.bus = None
            messagebox.showerror("Błąd połączenia CAN", str(exc))

    def _disconnect(self) -> None:
        self._abort_auto_tests()
        self.running = False
        self.mcu_sim_enable_var.set(False)
        if self.bus is not None:
            try:
                self.bus.shutdown()
            except Exception:
                pass
            self.bus = None
        self.connect_btn.configure(text="Połącz")
        self.conn_status_var.set("Rozłączony")
        self.conn_status_lbl.configure(foreground="red")
        self._log("[CAN] Rozłączono")

    # ------------------------------------------------------------------
    # Nadawanie CAN (bezpośrednio wg DBC, Extended ID)
    # ------------------------------------------------------------------
    def _send_raw(self, frame_id: int, data: bytes, desc: str, log_tx: bool = True) -> bool:
        if self.bus is None:
            self._log("[BŁĄD] Najpierw połącz się z magistralą CAN.")
            return False
        try:
            frame = can.Message(arbitration_id=frame_id, data=data, is_extended_id=True)
            self.bus.send(frame)
            if log_tx:
                hex_str = " ".join(f"{b:02X}" for b in data)
                self._log(f"TX | ID=0x{frame_id:03X} | DLC={len(data)} | {hex_str} | {desc}")
            return True
        except Exception as exc:
            self._log(f"[BŁĄD TX 0x{frame_id:03X}] {exc}")
            return False

    def _send_system_enable(self, val: int) -> bool:
        return self._send_raw(0x510, struct.pack("<B", 1 if val else 0), f"CMD_SYSTEM_ENABLE={1 if val else 0}")

    def _send_main_valve(self, val: int) -> bool:
        return self._send_raw(0x511, struct.pack("<B", 1 if val else 0), f"CMD_MAIN_VALVE={1 if val else 0}")

    def _send_manual_purge(self) -> bool:
        return self._send_raw(0x512, struct.pack("<B", 1), "CMD_MANUAL_PURGE=1")

    def _send_fan_cmd(self, fan_on: int, duty_override: int | None = None) -> bool:
        try:
            duty = duty_override if duty_override is not None else int(self.fan_duty_var.get().strip())
            duty = max(0, min(100, duty))
        except ValueError:
            self._log("[BŁĄD] Nieprawidłowa wartość PWM [%]")
            return False
        on_b = 1 if fan_on else 0
        duty_b = duty if on_b else 0
        return self._send_raw(0x513, struct.pack("<BB", on_b, duty_b), f"FAN_ON={on_b} DUTY={duty_b}%")

    def _send_fan_settings(
            self,
            mode: int | None = None,
            p: int | None = None,
            i: int | None = None,
            d: int | None = None,
    ) -> bool:
        try:
            m = FAN_MODE_MAP[self.fan_mode_var.get()] if mode is None else mode
            pg = max(0, min(1000, int(self.p_gain_var.get().strip()) if p is None else p))
            ig = max(0, min(1000, int(self.i_gain_var.get().strip()) if i is None else i))
            dg = max(0, min(1000, int(self.d_gain_var.get().strip()) if d is None else d))
        except ValueError:
            self._log("[BŁĄD] Nieprawidłowe nastawy PID")
            return False
        data = struct.pack("<BBHHH", m & 0x03, 0, pg, ig, dg)
        return self._send_raw(0x520, data, f"FAN_MODE={m} P={pg} I={ig} D={dg}")

    def _send_valve_settings(
            self,
            interval_s: int | None = None,
            duration_ms: int | None = None,
            mode: int | None = None,
            v_thr_mv: int | None = None,
            cooldown_s: int | None = None,
    ) -> bool:
        try:
            iv = int(self.purge_interval_var.get().strip()) if interval_s is None else interval_s
            du = int(self.purge_duration_var.get().strip()) if duration_ms is None else duration_ms
            mo = PURGE_MODE_MAP[self.purge_mode_var.get()] if mode is None else mode
            vt = int(self.purge_v_thr_var.get().strip()) if v_thr_mv is None else v_thr_mv
            cd = int(self.purge_cooldown_var.get().strip()) if cooldown_s is None else cooldown_s
        except ValueError:
            self._log("[BŁĄD] Nieprawidłowe parametry purge")
            return False
        data = struct.pack("<hHBHB", iv, du, mo & 0xFF, vt, cd & 0xFF)
        return self._send_raw(0x521, data, f"MODE={mo} INT={iv}s DUR={du}ms THR={vt}mV CD={cd}s")

    def _send_mcu_fc_voltage(self, voltage_v: float | None = None, log_tx: bool = True) -> bool:
        try:
            fc_v = float(self.mcu_v_var.get().strip().replace(",", ".")) if voltage_v is None else float(voltage_v)
        except ValueError:
            if log_tx:
                self._log("[BŁĄD] Nieprawidłowe napięcie FC [V]")
            return False
        raw_v = max(-32768, min(32767, int(round(fc_v * 1000.0))))
        data = struct.pack("<hhHh", 0, raw_v, 0, 0)
        return self._send_raw(0x104, data, f"MCU FC_V={fc_v:.3f} V ({raw_v} mV)", log_tx=log_tx)

    def _mcu_sim_loop(self) -> None:
        while self.running:
            if self.mcu_sim_enable_var.get() and not self.auto_test_running:
                self._send_mcu_fc_voltage(log_tx=self.log_104_tx_var.get())
            time.sleep(0.1)

    # ------------------------------------------------------------------
    # Odbiór CAN (bezpośrednio wg DBC)
    # ------------------------------------------------------------------
    def _rx_loop(self) -> None:
        while self.running and self.bus is not None:
            try:
                msg = self.bus.recv(timeout=0.05)
            except Exception as exc:
                self._log(f"[BŁĄD RX] {exc}")
                time.sleep(0.2)
                continue

            if msg is None:
                continue

            now = time.monotonic()
            fid = msg.arbitration_id
            data = bytes(msg.data)

            if fid not in FCCU_RX_IDS and not self.log_other_var.get():
                continue

            decoded_txt = ""
            with self.lock:
                if fid == 0x500 and len(data) >= 2:
                    b0, duty = data[0], data[1]
                    st = b0 & 0x0F
                    mv = (b0 >> 4) & 0x01
                    pv = (b0 >> 5) & 0x01
                    fs = (b0 >> 6) & 0x01

                    self.telemetry["state"] = st
                    self.telemetry["main_valve"] = mv
                    if pv == 1 and self.telemetry["prev_purge_valve"] == 0:
                        self.telemetry["purge_count"] += 1
                        self.telemetry["last_purge_ts"] = now
                    self.telemetry["prev_purge_valve"] = pv
                    self.telemetry["purge_valve"] = pv
                    self.telemetry["fan_state"] = fs
                    self.telemetry["fan_duty"] = duty
                    self.telemetry["last_rx_500"] = now
                    decoded_txt = f" | STATE={st} MAIN={mv} PURGE={pv} FAN={fs}/{duty}%"

                elif fid == 0x502 and len(data) >= 6:
                    lp, hp, lk = struct.unpack("<HHh", data[:6])
                    self.telemetry["h2_lp"] = lp * 0.01
                    self.telemetry["h2_hp"] = hp * 0.01
                    self.telemetry["leakage_v"] = lk * 0.01
                    decoded_txt = f" | LP={lp*0.01:.2f} HP={hp*0.01:.2f} LK={lk*0.01:.2f}"

                elif fid == 0x503 and len(data) >= 8:
                    fv, fa, dv, da = struct.unpack("<hhhh", data[:8])
                    self.telemetry["fc_v"] = fv * 0.01
                    self.telemetry["fc_a"] = fa * 0.01
                    self.telemetry["dcdc_v"] = dv * 0.01
                    self.telemetry["dcdc_a"] = da * 0.01
                    decoded_txt = f" | FC={fv*0.01:.2f}V/{fa*0.01:.2f}A"

                elif fid == 0x504 and len(data) >= 8:
                    ft, at, ah, ap = struct.unpack("<hhHH", data[:8])
                    self.telemetry["fc_temp_c"] = ft * 0.01
                    self.telemetry["amb_temp_c"] = at * 0.01
                    self.telemetry["amb_hum"] = ah * 0.01
                    self.telemetry["amb_press"] = ap * 0.01
                    self.telemetry["count_504"] += 1
                    self.telemetry["last_rx_504"] = now
                    decoded_txt = f" | FC_T={ft*0.01:.2f}C AMB={at*0.01:.2f}C {ah*0.01:.1f}% {ap*0.01:.1f}hPa"

                elif fid == 0x506 and len(data) >= 4:
                    (vol_raw,) = struct.unpack("<I", data[:4])
                    self.telemetry["h2_total"] = vol_raw * 0.001
                    self.telemetry["count_506"] += 1
                    self.telemetry["last_rx_506"] = now
                    decoded_txt = f" | H2_TOTAL={vol_raw*0.001:.3f} Ln"

                elif fid == 0x507 and len(data) >= 4:
                    (fl_raw,) = struct.unpack("<I", data[:4])
                    self.telemetry["h2_flow"] = fl_raw * 0.001
                    self.telemetry["count_507"] += 1
                    self.telemetry["last_rx_507"] = now
                    self.rx_507_times.append(now)
                    decoded_txt = f" | H2_FLOW={fl_raw*0.001:.3f} Ln/min"

            if fid == 0x507 and not self.log_507_var.get():
                continue

            hex_str = " ".join(f"{b:02X}" for b in data)
            self._log(f"RX | ID=0x{fid:03X} | DLC={len(data)} | {hex_str}{decoded_txt}")

    # ------------------------------------------------------------------
    # Automatyczne testy 1–6 + 7a/7b (Flash)
    # ------------------------------------------------------------------
    def _set_test_status(self, key: str, text: str, color: str) -> None:
        with self.lock:
            self.test_status[key] = (text, color)

    def _set_progress(self, text: str) -> None:
        with self.lock:
            self.auto_progress_text = text

    def _snapshot(self) -> tuple[dict, float]:
        now = time.monotonic()
        with self.lock:
            t = dict(self.telemetry)
            while self.rx_507_times and (now - self.rx_507_times[0]) > 1.0:
                self.rx_507_times.popleft()
            hz = float(len(self.rx_507_times))
        return t, hz

    def _sleep_abortable(self, seconds: float) -> bool:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if self.auto_test_stop.is_set() or not self.running:
                return False
            time.sleep(0.05)
        return True

    def _inject_104_for(self, voltage_v: float, duration_s: float) -> bool:
        end = time.monotonic() + duration_s
        while time.monotonic() < end:
            if self.auto_test_stop.is_set() or not self.running:
                return False
            self._send_mcu_fc_voltage(voltage_v=voltage_v, log_tx=self.log_104_tx_var.get())
            time.sleep(0.1)
        return True

    def _wait_condition(self, predicate, timeout_s: float) -> bool:
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            if self.auto_test_stop.is_set() or not self.running:
                return False
            t, hz = self._snapshot()
            if predicate(t, hz):
                return True
            time.sleep(0.05)
        return False

    def _start_auto_tests(self) -> None:
        if self.bus is None:
            messagebox.showwarning("Brak połączenia", "Najpierw połącz się z magistralą CAN.")
            return
        if self.auto_test_running:
            return

        self.mcu_sim_enable_var.set(False)
        self.auto_test_stop.clear()
        self.auto_test_running = True
        for key, _ in TEST_SPECS[:6]:
            self._set_test_status(key, "OCZEKUJE", "#555555")

        self.auto_test_thread = threading.Thread(target=self._run_auto_tests, daemon=True)
        self.auto_test_thread.start()

    def _abort_auto_tests(self) -> None:
        if self.auto_test_running:
            self.auto_test_stop.set()
            self._set_progress("Przerywanie testów...")

    def _run_auto_tests(self) -> None:
        self._log("========== START AUTOMATYCZNYCH TESTÓW FCCU (1–6) ==========")
        try:
            # TEST 1: Auto-start STATE_INIT -> STATE_RUNNING (bez wysyłania 0x510=1)
            self._set_progress("Test 1/6: Sprawdzanie auto-startu w RUNNING (0x500)...")
            self._set_test_status("t1_boot", "W TOKU...", "#AA6600")
            ok_rx = self._wait_condition(lambda t, _: t["last_rx_500"] is not None, timeout_s=2.5)
            t, _ = self._snapshot()
            if ok_rx and t["state"] == 2:
                self._set_test_status("t1_boot", "PASS (RUNNING=2)", "green")
            elif ok_rx:
                self._set_test_status("t1_boot", f"UWAGA (STATE={t['state']})", "#CC6600")
                self._send_system_enable(1)
                self._sleep_abortable(0.3)
            else:
                self._set_test_status("t1_boot", "FAIL (brak 0x500)", "red")
                self._set_progress("Przerwano: brak komunikacji 0x500 z FCCU")
                return

            # TEST 2: 0x506 (1 Hz) + 0x507 (~100 Hz)
            self._set_progress("Test 2/6: Pomiar częstotliwości 0x506 i 0x507 (1.6 s)...")
            self._set_test_status("t2_flow", "W TOKU...", "#AA6600")
            t0, _ = self._snapshot()
            c506_start = t0["count_506"]
            if not self._sleep_abortable(1.6):
                return
            t1, hz507 = self._snapshot()
            got_506 = (t1["count_506"] - c506_start) >= 1
            if got_506 and hz507 >= 50.0:
                self._set_test_status("t2_flow", f"PASS (0x507={hz507:.0f} Hz)", "green")
            else:
                self._set_test_status("t2_flow", f"FAIL (506={got_506}, 507={hz507:.0f}Hz)", "red")

            # TEST 3: Termistor FC 0x504
            self._set_progress("Test 3/6: Sprawdzanie ramki 0x504 (termistor FC)...")
            self._set_test_status("t3_temp", "W TOKU...", "#AA6600")
            t_env, _ = self._snapshot()
            if t_env["last_rx_504"] is not None:
                fc_t = t_env["fc_temp_c"]
                if fc_t != 0.0:
                    self._set_test_status("t3_temp", f"PASS ({fc_t:.2f} °C)", "green")
                else:
                    self._set_test_status("t3_temp", "UWAGA (0.00 °C — brak/odpięty czujnik)", "#CC6600")
            else:
                self._set_test_status("t3_temp", "FAIL (brak 0x504)", "red")

            # TEST 4: Wentylator 30% + STOPPED -> RUNNING resume
            self._set_progress("Test 4/6: Wentylator 30%, STOPPED -> RUNNING...")
            self._set_test_status("t4_fan", "W TOKU...", "#AA6600")
            self._send_fan_settings(mode=0, p=10, i=1, d=0)
            self._sleep_abortable(0.15)
            self._send_fan_cmd(1, duty_override=30)
            ok_on = self._wait_condition(
                lambda t, _: t["state"] == 2 and t["fan_state"] == 1 and t["fan_duty"] == 30, 1.5
            )
            self._send_system_enable(0)
            ok_stop = self._wait_condition(
                lambda t, _: t["state"] == 3 and t["fan_duty"] == 0 and t["main_valve"] == 0, 1.5
            )
            self._send_system_enable(1)
            ok_resume = self._wait_condition(
                lambda t, _: t["state"] == 2 and t["fan_state"] == 1 and t["fan_duty"] == 30, 1.5
            )
            if ok_on and ok_stop and ok_resume:
                self._set_test_status("t4_fan", "PASS (30% -> 0% -> 30%)", "green")
            else:
                self._set_test_status("t4_fan", f"FAIL (on={ok_on} stop={ok_stop} res={ok_resume})", "red")

            # TEST 5: Blokada auto-purge przy MAIN_VALVE = 0
            self._set_progress("Test 5/6: Blokada auto-purge przy MAIN_VALVE=0 (3.5 s)...")
            self._set_test_status("t5_lock", "W TOKU...", "#AA6600")
            self._send_main_valve(0)
            self._send_valve_settings(interval_s=2, duration_ms=100, mode=2, v_thr_mv=20000, cooldown_s=2)
            if not self._sleep_abortable(0.3):
                return
            t_b, _ = self._snapshot()
            p_b = t_b["purge_count"]
            if not self._inject_104_for(18.0, 3.5):
                return
            t_a, _ = self._snapshot()
            purges_closed = t_a["purge_count"] - p_b
            if purges_closed == 0:
                self._set_test_status("t5_lock", "PASS (0 przedmuchów)", "green")
            else:
                self._set_test_status("t5_lock", f"FAIL (wykonał {purges_closed} purge!)", "red")

            # TEST 6: Auto-purge przy MAIN_VALVE = 1 (VOLTAGE 18V, blokada <5V, TIME 2s)
            self._set_progress("Test 6/6: Auto-purge przy MAIN_VALVE=1 (V_FC=18V, 3V, TIME)...")
            self._set_test_status("t6_purge", "W TOKU...", "#AA6600")

            self._send_valve_settings(interval_s=0, duration_ms=100, mode=1, v_thr_mv=20000, cooldown_s=2)
            self._sleep_abortable(0.15)
            self._send_main_valve(1)
            self._sleep_abortable(0.15)

            t_v1, _ = self._snapshot()
            p_v1 = t_v1["purge_count"]
            if not self._inject_104_for(18.0, 3.5):
                return
            t_v2, _ = self._snapshot()
            ok_v_trig = (t_v2["purge_count"] - p_v1) >= 1

            self._sleep_abortable(0.2)
            t_low1, _ = self._snapshot()
            p_low1 = t_low1["purge_count"]
            if not self._inject_104_for(3.0, 2.8):
                return
            t_low2, _ = self._snapshot()
            ok_v_guard = (t_low2["purge_count"] - p_low1) == 0

            self._send_valve_settings(interval_s=2, duration_ms=100, mode=0, v_thr_mv=20000, cooldown_s=5)
            self._sleep_abortable(0.15)
            t_tm1, _ = self._snapshot()
            p_tm1 = t_tm1["purge_count"]
            if not self._sleep_abortable(3.0):
                return
            t_tm2, _ = self._snapshot()
            ok_time_trig = (t_tm2["purge_count"] - p_tm1) >= 1

            self._send_main_valve(0)

            if ok_v_trig and ok_v_guard and ok_time_trig:
                self._set_test_status("t6_purge", "PASS (V_FC + <5V + TIME)", "green")
            else:
                self._set_test_status(
                    "t6_purge", f"FAIL (V={ok_v_trig} <5V={ok_v_guard} T={ok_time_trig})", "red"
                )

            self._set_progress("Zakończono testy 1–6. Teraz możesz wykonać Test 7 (Flash).")
            self._log("========== KONIEC TESTÓW 1–6 ==========")
        finally:
            if self.bus is not None and self.running:
                self._send_main_valve(0)
            self.auto_test_running = False

    def _save_flash_preset(self) -> None:
        if self.bus is None:
            messagebox.showwarning("Brak połączenia", "Najpierw połącz się z magistralą CAN.")
            return
        self._send_fan_settings(mode=0, p=12, i=2, d=1)
        time.sleep(0.1)
        self._send_fan_cmd(1, duty_override=30)
        time.sleep(0.1)
        self._send_valve_settings(interval_s=10, duration_ms=100, mode=2, v_thr_mv=20000, cooldown_s=5)
        self._set_test_status("t7_flash", "ZAPISANO -> ZRESTARTUJ FCCU", "#0055AA")
        self._set_progress("Odetnij i podłącz zasilanie FCCU (lub wciśnij RESET), potem kliknij 7b.")
        self._log("[TEST 7a] Zapisano preset w Flash (FAN MANUAL ON 30%, PID 12/2/1, PURGE BOTH 10s/100ms/20V/5s).")

    def _verify_flash_after_reboot(self) -> None:
        if self.bus is None:
            messagebox.showwarning("Brak połączenia", "Najpierw połącz się z magistralą CAN.")
            return
        t, _ = self._snapshot()
        now = time.monotonic()
        fresh = t["last_rx_500"] is not None and (now - t["last_rx_500"]) < 2.5
        if fresh and t["state"] == 2 and t["fan_state"] == 1 and t["fan_duty"] == 30 and t["main_valve"] == 0:
            self._set_test_status("t7_flash", "PASS (RUNNING + FAN 30%)", "green")
            self._set_progress("Test 7 PASS: FCCU samo przeszło INIT->RUNNING z wentylatorem 30% z Flash.")
            self._log("[TEST 7b] PASS — po restarcie STATE=RUNNING(2), FAN=ON 30%, MAIN_VALVE=0.")
        else:
            self._set_test_status(
                "t7_flash",
                f"FAIL (ST={t['state']} FAN={t['fan_state']}/{t['fan_duty']}%)",
                "red",
            )
            self._log(
                f"[TEST 7b] FAIL — fresh={fresh}, STATE={t['state']}, FAN={t['fan_state']}/{t['fan_duty']}%, MAIN={t['main_valve']}"
            )

    # ------------------------------------------------------------------
    # Odświeżanie GUI (10 Hz)
    # ------------------------------------------------------------------
    def _refresh_ui(self) -> None:
        now = time.monotonic()
        t, hz_507 = self._snapshot()

        if t["state"] is not None:
            self.tel_vars["state"].set(STATE_LABELS.get(t["state"], str(t["state"])))
            m_txt = "OPEN (1)" if t["main_valve"] else "CLOSED (0)"
            p_txt = "PURGING (1)" if t["purge_valve"] else "CLOSED (0)"
            self.tel_vars["valves"].set(f"MAIN: {m_txt}  |  PURGE: {p_txt}")
            self.tel_vars["fan_status"].set(f"{'ON' if t['fan_state'] else 'OFF'}  |  {t['fan_duty']}%")

        if t["last_purge_ts"] is not None:
            ago = now - t["last_purge_ts"]
            self.tel_vars["purge_stats"].set(f"{t['purge_count']} szt. ({ago:.1f} s temu)")
        else:
            self.tel_vars["purge_stats"].set(f"{t['purge_count']} szt.")

        self.tel_vars["h2_total"].set(f"{t['h2_total']:.3f} Ln  (rx: {t['count_506']})")
        self.tel_vars["h2_flow"].set(f"{t['h2_flow']:.3f} Ln/min")
        self.tel_vars["hz_507"].set(f"{hz_507:.1f} Hz  (rx: {t['count_507']})")
        self.tel_vars["flow_press"].set(
            f"LP {t['h2_lp']:.2f} | HP {t['h2_hp']:.2f} bar | LK {t['leakage_v']:.2f} V"
        )
        self.tel_vars["fc_temp"].set(f"{t['fc_temp_c']:.2f} °C")
        self.tel_vars["bme280"].set(
            f"{t['amb_temp_c']:.2f} °C | {t['amb_hum']:.1f} % | {t['amb_press']:.1f} hPa"
        )
        self.tel_vars["power"].set(
            f"FC: {t['fc_v']:.2f} V / {t['fc_a']:.2f} A  |  DCDC: {t['dcdc_v']:.2f} V / {t['dcdc_a']:.2f} A"
        )

        with self.lock:
            self.auto_progress_var.set(self.auto_progress_text)
            for key, (txt, color) in self.test_status.items():
                self.test_lbls[key].configure(text=txt, foreground=color)

        lines = []
        for _ in range(100):
            try:
                lines.append(self.log_queue.get_nowait())
            except queue.Empty:
                break

        if lines:
            self.log_text.configure(state="normal")
            self.log_text.insert("end", "\n".join(lines) + "\n")
            line_count = int(self.log_text.index("end-1c").split(".")[0])
            if line_count > 700:
                self.log_text.delete("1.0", f"{line_count - 550}.0")
            self.log_text.see("end")
            self.log_text.configure(state="disabled")

        self.root.after(100, self._refresh_ui)

    def _log(self, msg: str) -> None:
        ts = time.strftime("%H:%M:%S")
        try:
            self.log_queue.put_nowait(f"[{ts}] {msg}")
        except queue.Full:
            pass

    def _clear_log(self) -> None:
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def _reset_counters(self) -> None:
        with self.lock:
            self.telemetry["count_504"] = 0
            self.telemetry["count_506"] = 0
            self.telemetry["count_507"] = 0
            self.telemetry["purge_count"] = 0
            self.telemetry["last_purge_ts"] = None
            self.rx_507_times.clear()

    def _on_close(self) -> None:
        self._disconnect()
        self.root.destroy()


if __name__ == "__main__":
    app_root = tk.Tk()
    FCCUTesterApp(app_root)
    app_root.mainloop()
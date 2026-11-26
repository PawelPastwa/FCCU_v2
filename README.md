# Kompletna Dokumentacja Projektu FCCU (Fuel Cell Control Unit)

## 1. Czym jest FCCU i jak zbudowany jest cały projekt?

### 1.1. Rola sterownika FCCU w pojeździe wodorowym

Sercem napędu naszego pojazdu jest **ogniwo paliwowe** (*Fuel Cell*, w skrócie **FC**). Ogniwo łączy wodór ($\text{H}_2$) ze zbiornika z tlenem ($\text{O}_2$) z powietrza, wytwarzając w ten sposób prąd elektryczny, ciepło oraz czystą wodę ($\text{H}_2\text{O}$).

Ogniwo paliwowe jest jednak urządzeniem delikatnym i wymagającym ciągłego nadzoru. Nie można po prostu odkręcić butli i zostawić go samego sobie. Ktoś musi nieustannie pilnować kilku kluczowych rzeczy:

* otwierać i zamykać **główny zawór wodoru** (aby gaz płynął tylko wtedy, gdy pojazd jest gotowy i bezpieczny),

* regularnie lub w razie spadku napięcia otwierać na ułamek sekundy **zawór przedmuchowy** (*purge valve*), żeby wydmuchać z ogniwa zbierającą się wodę i azot, które „duszą” reakcję,

* płynnie sterować **wentylatorem chłodzącym**, utrzymując ogniwo w optymalnej temperaturze,

* mierzyć **temperaturę, ciśnienie, chwilowy przepływ oraz łączne zużycie wodoru**,

* natychmiast **odciąć wszystko w razie awarii lub utraty łączności**,

* na bieżąco **wymieniać dane z resztą pojazdu** — głównym komputerem **MCU**, kierownicą **SWU**, telemetrią **TCU** i pozostałymi modułami.

Tym właśnie dedykowanym „mózgiem” ogniwa jest **FCCU** (*Fuel Cell Control Unit*). Zastępuje ono fabryczny sterownik *Spectronic Protium*, dając zespołowi pełną kontrolę nad parametrami pracy ogniwa. Wydzielenie obsługi ogniwa do osobnego sterownika zamiast dokładania jej do głównego komputera MCU sprawia, że MCU nie jest przeciążone (może skupić się na napędzie, pedałach i bezpieczeństwie całego pojazdu), a sam układ FCCU można łatwo wymontować, przetestować na biurku na zasilaczu laboratoryjnym albo przenieść do kolejnej generacji bolidu.

### 1.2. Sprzęt: mikrokontroler ESP32-S3 i płytka `fccu_v2`

Fizycznym sercem FCCU jest układ scalony **ESP32-S3** wlutowany na autorskiej płytce drukowanej **`fccu_v2`**. Mikrokontroler to kompletny miniaturowy komputer zamknięty w jednym układzie: ma szybki procesor, pamięć roboczą **RAM** (ulotną — trzyma bieżące zmienne podczas pracy), pamięć trwałą **Flash** (nieulotną — pamięta program i zapisane ustawienia po odłączeniu akumulatora) oraz zestaw uniwersalnych nóżek **GPIO** (*General Purpose Input/Output*).

ESP32-S3 idealnie pasuje do tego zadania, ponieważ ma w sobie wszystko, czego potrzebujemy, bez konieczności doklejania wielu zewnętrznych układów: obsługuje magistralę CAN, generuje sprzętowe sygnały PWM dla wentylatora, posiada przetworniki analogowo-cyfrowe ADC oraz kontrolery I2C. Z kolei sama płytka `fccu_v2` dostarcza układy mocy (tranzystory sterujące cewkami zaworów), przetwornice zasilania i złącza odporne na warunki panujące w pojeździe.

### 1.3. System operacyjny Zephyr RTOS

Na zwykłym komputerze system Windows lub Linux pozwala jednocześnie słuchać muzyki, pisać tekst i pobierać pliki. **Zephyr RTOS** (*Real-Time Operating System* — system operacyjny czasu rzeczywistego) robi to samo na mikrokontrolerze ESP32-S3, ale z jedną kluczową zaletą: **działa z przewidywalną dokładnością co do milisekundy**.

Gdybyśmy pisali ten program bez systemu operacyjnego (tzw. *bare-metal* albo klasyczne Arduino z funkcjami `delay()`), każda wolniejsza czynność — na przykład odczyt czujnika albo zapis ustawień do pamięci Flash — zatrzymywałaby na chwilę cały procesor. W efekcie sterownik spóźniałby się z wysyłaniem szybkich ramek przepływu wodoru albo reagowałby z opóźnieniem na komendę awaryjnego zamknięcia zaworu. Zephyr rozwiązuje ten problem, dając nam wielowątkowość, bezpieczne kolejki zdarzeń, sprzętowy watchdog, system plików NVS w pamięci Flash oraz sprawdzone sterowniki komunikacji. Dodatkowo z Zephyra korzystają inne sterowniki w naszym zespole (m.in. SWU i LCU), co ujednolica narzędzia (`west`) i ułatwia wymianę wiedzy między członkami koła.

### 1.4. Mapa plików projektu

Zamiast wrzucać cały kod do jednego gigantycznego pliku, w którym po miesiącu nikt nie potrafiłby niczego znaleźć bez psucia reszty, projekt podzielono na logiczne warstwy:

| Folder / Plik | Rola w projekcie (prostym językiem) | 
| ----- | ----- | 
| `src/main.c` | **Dyrygent całego programu.** Uruchamia moduły, odbiera zdarzenia ze skrzynki, pilnuje stanów pracy i koordynuje zawory, wentylator oraz telemetrię. | 
| `src/fccu_config.h` | **Tablica stałych ustawień.** Tutaj znajdują się progi, okres wysyłki szybkiego przepływu (`FCCU_FLOW_FAST_PERIOD_MS`), przełącznik źródła temperatury (`FCCU_USE_FC_TEMP`) oraz parametry pomiarowe. | 
| `src/fccu_events.h` | **Słownik zdarzeń.** Lista wszystkich komunikatów („karteczek”), które mogą trafić do kolejki zdarzeń w `main.c`. | 
| `src/core/fccu_state.*` | **Maszyna stanów.** Czysta logika decydująca, czy sterownik znajduje się w stanie `INIT`, `STARTING`, `RUNNING` czy `STOPPED`. | 
| `src/core/fccu_valves.*` | **Logika zaworów.** Oblicza, kiedy otworzyć zawór główny i kiedy uruchomić automatyczny przedmuch (czasowy lub napięciowy). | 
| `src/core/fccu_fan.*` | **Logika wentylatora.** Przelicza temperaturę i nastawy PID na konkretny procent mocy nawiewu ($0\text{--}100\%$). | 
| `src/core/fccu_flow.*` | **Kalkulator wodoru.** Zamienia odstęp czasu między impulsami z przepływomierza na przepływ chwilowy i całkowitą objętość zużytego gazu. | 
| `src/bsp/fccu_actuators.*` | **Ręce sterownika.** Fizycznie wysterowuje zawory i sygnał PWM wentylatora; potrafi też natychmiast wszystko odciąć. | 
| `src/bsp/fccu_sensors.*` | **Zmysły sterownika.** Czyta napięcia z przetworników ADC, zlicza impulsy przepływomierza i pobiera dane z czujników I2C. | 
| `src/bsp/fccu_button.*` | **Obsługa przycisku.** Rozpoznaje pojedyncze oraz podwójne kliknięcie przycisku na płytce. | 
| `src/bsp/fccu_storage.*` | **Pamięć trwała.** Zapisuje i odczytuje konfigurację z pamięci Flash (NVS). | 
| `src/bsp/fccu_watchdog.*` | **Strażnik systemu.** Pilnuje, aby procesor zrestartował się, gdyby program z jakiegokolwiek powodu zawisł. | 
| `src/bsp/system_status.*` | **Sygnalizacja.** Obsługuje sygnalizację stanu płytki (np. diodę statusu). | 
| `src/comm/fccu_can.*` | **Tłumacz sieci CAN.** Pakuje liczby w ramki CAN do wysłania i rozpakowuje rozkazy przychodzące z pojazdu. | 
| `boards/espressif/fccu_v2/` | **Mapa elektroniki.** Pliki Devicetree (`.dts` i `-pinctrl.dtsi`) informujące Zephyra, do której nóżki procesora podpięty jest konkretny układ. | 
| `CMakeLists.txt` | **Przepis na zbudowanie programu.** Mówi kompilatorowi, które pliki `.c` i `.h` (w tym bibliotekę `lib-can-definitions`) połączyć w jeden plik binarny. | 

## 2. Rozbiór kodu krok po kroku: `src/main.c`

Poniżej omówiono **dosłownie każdy blok i każdą funkcję** w głównym pliku programu `src/main.c` — w dokładnie takiej kolejności, w jakiej występują one w kodzie.

### 2.1. Dołączanie nagłówków, logi i zmienne stanu (góra pliku)

```
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fccu_config.h"
#include "fccu_events.h"
#include "core/fccu_state.h"
...
LOG_MODULE_REGISTER(fccu_main, LOG_LEVEL_INF);

K_MSGQ_DEFINE(fccu_event_q, sizeof(fccu_event_t), 16, 4);

static fccu_state_ctx_t    state_ctx;
static fccu_valves_ctx_t   valves_ctx;
static fccu_fan_ctx_t      fan_ctx;
static fccu_flow_ctx_t     flow_ctx;
static fccu_sensors_data_t sensors_data;

```

**Co tu się dzieje?**

* **Blok `#include`:** podłącza „spisy treści” (pliki nagłówkowe `.h`) z systemu Zephyr (`kernel.h` dla wątków, czasu i kolejek; `log.h` dla komunikatów w konsoli KiTTY) oraz z naszych własnych modułów (`core/`, `bsp/`, `comm/`). Dzięki temu `main.c` wie, jak wywoływać funkcje napisane w pozostałych plikach.

* **`LOG_MODULE_REGISTER(fccu_main, LOG_LEVEL_INF);`:** rejestruje ten plik w systemie logowania pod etykietą `fccu_main`. Poziom `LOG_LEVEL_INF` oznacza, że na porcie szeregowym (np. w KiTTY) zobaczymy ważne informacje (`LOG_INF`), ostrzeżenia (`LOG_WRN`) i błędy (`LOG_ERR`), ale bez zalewania konsoli drobiazgowymi komunikatami debugowymi (`LOG_DBG`).

* **`K_MSGQ_DEFINE(fccu_event_q, sizeof(fccu_event_t), 16, 4);`:** tworzy globalną kolejkę zdarzeń o nazwie `fccu_event_q`. Każda „karteczka” w kolejce ma rozmiar struktury `fccu_event_t`, kolejka pomieści do **16 oczekujących zdarzeń** naraz, a liczba `4` wyrównuje pamięć do 4 bajtów (wymóg procesorów 32-bitowych, żeby odczyt był szybki i bezpieczny). Kolejka celowo **nie ma** słowa `static`, ponieważ inne pliki (`fccu_can.c`, `fccu_button.c`, `fccu_actuators.c`) muszą mieć do niej dostęp, aby wrzucać tam nowe zdarzenia.

* **Pięć zmiennych `static ..._ctx` i `sensors_data`:** to „teczki z aktualnymi danymi” (konteksty) poszczególnych modułów. Słowo `static` przed zmienną na górze pliku oznacza, że **tylko `main.c` może bezpośrednio dotykać tych struktur** — pozostałe pliki dostają je wyłącznie wtedy, gdy `main.c` przekaże im adres (np. `&fan_ctx`). Zapobiega to przypadkowemu nadpisaniu stanu przez inną część programu.

### 2.2. Funkcja `get_fan_temp_c(void)` – bezpieczne przygotowanie temperatury dla wentylatora

```
static uint8_t get_fan_temp_c(void) {
    int16_t temp_x100 =
#if FCCU_USE_FC_TEMP
            sensors_data.fc_temp_x100;
#else
            sensors_data.ambient_temp_x100;
#endif
    return (temp_x100 > 0) ? (uint8_t)(temp_x100 / 100) : 0U;
}

```

**Co tu się dzieje?**

1. Dyrektywa preprocesora `#if FCCU_USE_FC_TEMP` działa jeszcze na etapie kompilacji: jeśli w `fccu_config.h` flaga jest ustawiona na `1`, wentylator będzie brał temperaturę z termistora na ogniwie (`fc_temp_x100`). Jeśli na `0` — z czujnika otoczenia BME/BMP280 (`ambient_temp_x100`).

2. Temperatury w projekcie przechowujemy jako liczby całkowite pomnożone przez $100$ (np. $25{,}34^\circ\text{C}$ to liczba `2534`), co eliminuje błędy liczb zmiennoprzecinkowych.

3. Operator warunkowy `(temp_x100 > 0) ? (uint8_t)(temp_x100 / 100) : 0U` dzieli temperaturę przez `100`, aby uzyskać pełne stopnie Celsjusza, ale wcześniej sprawdza, czy temperatura jest dodatnia. To **kluczowe zabezpieczenie**: gdy termistor jest odpięty, zwraca wartość błędu $-50{,}00^\circ\text{C}$ (`-5000`). Bez warunku `> 0` zamiana liczby ujemnej `-50` na typ bez znaku `uint8_t` przekręciłaby ją na **`206`** ($206^\circ\text{C}$!), przez co regulator uznałby, że ogniwo się pali, i włączyłby wentylator na $100\%$ mocy. Dzięki tej linijce ujemny wynik bezpiecznie zamienia się w `0U` ($0^\circ\text{C}$).

### 2.3. Funkcja `save_settings_to_flash(void)` – pakowanie i zapis ustawień do pamięci trwałej

```
static void save_settings_to_flash(void) {
    fccu_persisted_cfg_t cfg = {
            .fan_mode                   = fan_ctx.mode,
            .fan_manual_on              = fan_ctx.manual_state,
            .fan_manual_duty            = fan_ctx.manual_duty,
            .fan_p_gain                 = fan_ctx.p_gain,
            .fan_i_gain                 = fan_ctx.i_gain,
            .fan_d_gain                 = fan_ctx.d_gain,
            .purge_mode                 = valves_ctx.purge_mode,
            .purge_interval_s           = valves_ctx.purge_interval_s,
            .purge_duration_ms          = valves_ctx.purge_duration_ms,
            .purge_voltage_threshold_mv = valves_ctx.purge_voltage_threshold_mv,
            .purge_voltage_cooldown_s   = valves_ctx.purge_voltage_cooldown_s
    };
    (void)fccu_storage_save(&cfg);
}

```

**Co tu się dzieje?**

1. Funkcja tworzy tymczasową paczkę `cfg` typu `fccu_persisted_cfg_t` i przepisuje do niej wszystkie aktualne ustawienia wentylatora (z `fan_ctx`) oraz zaworu przedmuchowego (z `valves_ctx`).

2. Następnie wywołuje `fccu_storage_save(&cfg)`, która zapisuje tę paczkę w nieulotnej pamięci Flash (NVS).

3. Zapis `(void)` przed wywołaniem funkcji to sygnał dla kompilatora: *„wiemy, że funkcja zwraca kod błędu, ale celowo go tutaj nie sprawdzamy (moduł `fccu_storage` sam loguje ewentualny błąd zapisu)”*. Dzięki temu kompilacja przebiega czysto, bez ostrzeżeń.

### 2.4. Funkcja `load_settings_from_flash(void)` – przywracanie ustawień po restarcie

```
static void load_settings_from_flash(void) {
    fccu_persisted_cfg_t cfg;
    if (!fccu_storage_load(&cfg)) {
        return;
    }

    fccu_fan_set_settings(&fan_ctx, cfg.fan_mode, cfg.fan_p_gain, cfg.fan_i_gain, cfg.fan_d_gain);
    fccu_fan_set_manual_duty(&fan_ctx, cfg.fan_manual_on, cfg.fan_manual_duty);
    fccu_fan_resume(&fan_ctx);

    fccu_valves_set_settings(&valves_ctx,
                             cfg.purge_interval_s,
                             cfg.purge_duration_ms,
                             true,
                             cfg.purge_mode,
                             cfg.purge_voltage_threshold_mv,
                             cfg.purge_voltage_cooldown_s);

    LOG_INF("Restored Flash cfg: ...");
}

```

**Co tu się dzieje?**

1. Próbuje wczytać zapisaną strukturę `cfg` z pamięci Flash (`fccu_storage_load`). Jeśli pamięć jest jeszcze pusta (np. czysta płytka po pierwszym wgraniu) albo uszkodzona, funkcja zwraca `false` i natychmiast wychodzi (`return;`) — program pracuje wtedy na bezpiecznych ustawieniach domyślnych.

2. Jeśli odczyt się udał, funkcja kolejno:

   * wpisuje do modułu wentylatora jego tryb i nastawy `P`, `I`, `D`,

   * wpisuje ręczny stan włączenia oraz wypełnienie PWM (np. $30\%$),

   * wywołuje `fccu_fan_resume(&fan_ctx)`, aby odtworzyć roboczy stan wentylatora zgodny z wczytaną konfiguracją,

   * wpisuje wszystkie parametry przedmuchu (z flagą `true` oznaczającą, że wczytano pełny zestaw parametrów wraz z konfiguracją napięciową).

3. Na koniec wypisuje w konsoli jedno czytelne podsumowanie `Restored Flash cfg: ...`, dzięki któremu podczas testów w KiTTY od razu widać, że pamięć Flash zadziałała.

### 2.5. Funkcja `trigger_purge(uint32_t now_ms)` – uruchamianie pojedynczego przedmuchu

```
static void trigger_purge(uint32_t now_ms) {
    bool is_running = (state_ctx.state == STATE_RUNNING);
    if (fccu_valves_start_purge(&valves_ctx, is_running, now_ms)) {
        /* Nie zerujemy objętości przy purge — zliczamy sumaryczne zużycie od włączenia zasilania */
        fccu_actuators_set_purge_valve(1U);
        fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
        fccu_can_send_state((uint8_t)state_ctx.state, valves_ctx.main_valve,
                            valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
    }
}

```

**Co tu się dzieje?**

1. Sprawdza, czy system jest w stanie `STATE_RUNNING`. Jeśli nie — zmienna `is_running` wynosi `false`.

2. Pyta moduł logiki zaworów (`fccu_valves_start_purge`), czy w tej chwili **wolno** otworzyć przedmuch (moduł odrzuci prośbę, jeśli układ nie jest w stanie `RUNNING` albo jeśli przedmuch już trwa).

3. Jeśli logika wyraziła zgodę (zwróciła `true`):

   * **nie zeruje** licznika zużycia wodoru (`flow_ctx`),

   * fizycznie podaje napięcie na cewkę zaworu przedmuchowego (`fccu_actuators_set_purge_valve(1U)`),

   * uruchamia w tle timer zamykający zawór po dokładnie `valves_ctx.purge_duration_ms` milisekundach (bez zatrzymywania procesora),

   * od razu wysyła na CAN ramkę `FCCU_STATE`, żeby reszta pojazdu natychmiast widziała, że zawór przedmuchowy jest otwarty.

### 2.6. Funkcja `handle_event(const fccu_event_t *evt)` – centrum obsługi wszystkich zdarzeń

Ta funkcja dostaje pojedynczą „karteczkę” wyjętą z kolejki `fccu_event_q` i w bloku `switch (evt->type)` decyduje, co dokładnie zrobić. Poniżej opis każdego z **9 bloków `case`**:

1. **`case FCCU_EVT_CAN_SYSTEM_ENABLE:`**

   * Przekazuje do maszyny stanów (`fccu_state_set_enable`) komendę `0` (wyłącz) lub `1` (włącz) wraz z czasem jej nadejścia (`evt->timestamp_ms`). Samo fizyczne wyłączenie lub włączenie wyjść wykona się chwilę później w pętli głównej przy wykryciu zmiany stanu.

2. **`case FCCU_EVT_CAN_MAIN_VALVE:`**

   * Odnotowuje aktywność sieci CAN (`fccu_state_notify_can_rx`).

   * Zapamiętuje poprzedni stan zaworu głównego (`prev_main`), po czym zapisuje nowy żądany stan (`fccu_valves_set_main`).

   * Sprawdza warunek `if (prev_main == 0U && valves_ctx.main_valve == 1U)` — czyli **moment przejścia z zamkniętego na otwarty** (tzw. zbocze narastające). Tylko wtedy zeruje odliczanie timera przedmuchu (`fccu_valves_sync_purge_timer`), aby nie strzelić przedmuchem w pierwszej milisekundzie otwarcia zaworu. Gdyby ktoś ponownie wysłał „otwórz” dla już otwartego zaworu, timer nie zostanie zresetowany.

   * Jeśli układ jest w `STATE_RUNNING`, natychmiast przełącza fizyczny zawór i wysyła `FCCU_STATE`.

3. **`case FCCU_EVT_CAN_MANUAL_PURGE:`**

   * Odnotowuje ruch na CAN i wywołuje opisaną wyżej funkcję `trigger_purge(evt->timestamp_ms)`.

4. **`case FCCU_EVT_CAN_FAN_CMD:`**

   * Odnotowuje ruch na CAN, zapisuje ręczne włączenie i procent mocy (`fccu_fan_set_manual_duty`), a następnie **natychmiast zapisuje konfigurację we Flash** (`save_settings_to_flash()`).

   * W stanie `STATE_RUNNING` od razu zmienia sygnał PWM wentylatora i raportuje to przez `fccu_can_send_state()`.

5. **`case FCCU_EVT_CAN_FAN_SETTINGS:`**

   * Odnotowuje ruch na CAN, aktualizuje tryb wentylatora (`MANUAL`/`LINEAR_TEMP`/`PID`) i nastawy `P`, `I`, `D` (`fccu_fan_set_settings`), po czym **zapisuje konfigurację we Flash**.

   * W stanie `STATE_RUNNING` od razu aktualizuje PWM i wysyła `FCCU_STATE`.

6. **`case FCCU_EVT_CAN_VALVE_SETTINGS:`**

   * Odnotowuje ruch na CAN i aktualizuje parametry przedmuchu w `valves_ctx`. Zwróć uwagę na czwarty argument `(evt->data.valve_settings.has_voltage_cfg != 0U)` — dzięki niemu zachowana jest pełna kompatybilność wsteczna: jeśli z pojazdu przyjdzie starsza, krótka ramka 4-bajtowa (bez ustawień napięcia), moduł zmieni tylko interwał i czas trwania, nie psując zapisanych wcześniej progów napięciowych. Jeśli przyjdzie nowa ramka 8-bajtowa, zaktualizuje wszystko.

   * Zapisuje nowe ustawienia we Flash (`save_settings_to_flash()`).

7. **`case FCCU_EVT_BTN_SINGLE_CLICK:`**

   * Działa wyłącznie w `STATE_RUNNING`. Przełącza zawór główny na przeciwny (`fccu_valves_toggle_main`: z otwartego na zamknięty lub odwrotnie). Przy otwarciu synchronizuje timer przedmuchu, przełącza fizyczny zawór i wysyła `FCCU_STATE`.

8. **`case FCCU_EVT_BTN_DOUBLE_CLICK:`**

   * Uruchamia ręczny przedmuch (`trigger_purge`) bezpośrednio z przycisku na płytce (przydatne przy serwisowaniu na stanowisku bez podłączonego komputera).

9. **`case FCCU_EVT_PURGE_TIMEOUT:`**

   * Przychodzi z timera po upływie `purge_duration_ms`. Zamyka zawór w logice (`fccu_valves_close_purge`), odcina napięcie z cewki (`fccu_actuators_set_purge_valve(0U)`) i wysyła `FCCU_STATE` na CAN.

### 2.7. Wątek szybkiego przepływu `flow_fast_thread_fn` i makro `K_THREAD_DEFINE`

```
static void flow_fast_thread_fn(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        fccu_flow_snapshot_t snap;
        fccu_sensors_get_flow_snapshot(&snap);
        uint32_t flow_x1000 = fccu_flow_calc_x1000_from_period_us(snap.effective_period_us);
        fccu_can_send_flow_fast(flow_x1000);
        k_sleep(K_MSEC(FCCU_FLOW_FAST_PERIOD_MS));
    }
}

K_THREAD_DEFINE(flow_fast_tid, 1024, flow_fast_thread_fn, NULL, NULL, NULL, 6, 0, 100);

```

**Co tu się dzieje?**

* **Funkcja `flow_fast_thread_fn`:** to ciało niezależnego, równoległego wątku. Zephyr wymaga, aby każda funkcja wątku przyjmowała trzy wskaźniki (`p1`, `p2`, `p3`). Ponieważ ich nie potrzebujemy, wywołujemy makra `ARG_UNUSED(...)`, żeby kompilator nie ostrzegał o nieużywanych parametrach.

* **Pętla `while (1)` wątku:**

  1. Pobiera spójną „migawkę” z przepływomierza (`fccu_sensors_get_flow_snapshot(&snap)`) — przede wszystkim efektywny odstęp czasu między impulsami w mikrosekundach (`effective_period_us`).

  2. Przelicza ten czas na chwilowy przepływ ze skalą $\times 1000$ (`fccu_flow_calc_x1000_from_period_us`), czyli z dokładnością do 3 miejsc po przecinku.

  3. Natychmiast wysyła wynik w osobnej ramce CAN `0x507` (`fccu_can_send_flow_fast`).

  4. Usypia ten wątek na dokładnie `FCCU_FLOW_FAST_PERIOD_MS` milisekund (`k_sleep`), oddając w tym czasie procesor głównej pętli `main()`.

* **Makro `K_THREAD_DEFINE(...)`:** zleca Zephyrowi automatyczne utworzenie i uruchomienie tego wątku. Poszczególne liczby oznaczają:

  * `flow_fast_tid` – nazwa identyfikatora wątku,

  * `1024` – rozmiar prywatnej pamięci roboczej (stosu) w bajtach ($1\text{ KB}$ — wystarczająco dla prostych obliczeń i wysyłki CAN),

  * `flow_fast_thread_fn` – nazwa funkcji do uruchomienia,

  * `NULL, NULL, NULL` – brak argumentów wejściowych,

  * `6` – priorytet wątku (w Zephyrze mniejsza liczba oznacza wyższy priorytet; domyślny wątek `main` ma zazwyczaj priorytet `0`, więc `6` gwarantuje, że szybka telemetria nigdy nie zagłodzi głównej pętli bezpieczeństwa w `main()`),

  * `0` – brak specjalnych opcji wątku,

  * `100` – **opóźnienie startu wynoszące** $100\text{ ms}$. Dzięki temu wątek zaczyna nadawać ramki dopiero wtedy, gdy `main()` zdążył już zainicjalizować kontroler CAN i czujniki.

### 2.8. Funkcja `main(void)` – blok po bloku

Funkcja `main()` podzielona jest na cztery czytelne części: start, przygotowanie pętli oraz trzy wyraźne kroki wykonywane w kółko w `while (1)`.

#### Blok A: Inicjalizacja logiki i test startowy sprzętu

```
    init_system_status();
    fccu_state_init(&state_ctx);
    fccu_valves_init(&valves_ctx);
    fccu_fan_init(&fan_ctx);
    fccu_flow_init(&flow_ctx);

    if (fccu_actuators_init() != 0 ||
        fccu_button_init() != 0 ||
        fccu_sensors_init() != 0 ||
        fccu_can_init() != 0 ||
        fccu_watchdog_init() != 0) {
        LOG_ERR("Hardware init failed! Entering safe halt.");
        fccu_actuators_emergency_stop();
        return -1;
    }

```

Najpierw zerowane są wszystkie struktury logiczne w pamięci RAM. Potem uruchamiane są kolejno podzespoły sprzętowe. Zapis z operatorem `||` (LUB) działa jak szereg bezpieczników: jeśli **choć jeden** element zwróci błąd (wartość inną niż `0`), uruchamianie natychmiast się przerywa, wszystkie wyjścia zostają siłowo wyłączone (`fccu_actuators_emergency_stop()`), w konsoli pojawia się komunikat błędu, a program kończy pracę kodem `-1`.

#### Blok B: Odczyt Flash i ustalenie stanu startowego

```
    if (fccu_storage_init() == 0) {
        load_settings_from_flash();
    }

    fccu_sensors_read_all(&sensors_data);
    uint32_t boot_ms = k_uptime_get_32();
    fccu_state_t boot_state = fccu_state_step(&state_ctx, boot_ms);

    if (boot_state == STATE_RUNNING) {
        fccu_valves_sync_purge_timer(&valves_ctx, boot_ms);
        fccu_fan_resume(&fan_ctx);
        fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
        fccu_actuators_set_fan_duty(fan_ctx.duty);
    } else {
        fccu_actuators_emergency_stop();
    }

    fccu_can_send_state((uint8_t)boot_state, valves_ctx.main_valve,
                        valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);

    LOG_INF("FCCU started in state=%u (fan duty=%u%%)", (unsigned int)boot_state, fan_ctx.duty);

```

Pamięć Flash inicjalizowana jest osobno — jeśli z jakiegoś powodu zawiedzie, układ nadal może bezpiecznie pracować na wartościach domyślnych (dlatego brak Flash nie blokuje startu tak jak awaria zaworów czy CAN). Następnie wykonywany jest pierwszy odczyt wszystkich czujników (żeby wentylator znał temperaturę od pierwszej milisekundy), pobierany czas startu (`k_uptime_get_32()`) i sprawdzany stan startowy `boot_state`. Jeśli FCCU od razu uruchamia się w `STATE_RUNNING`, natychmiast przywraca moc wentylatora (np. zapamiętane $30\%$). Na koniec wysyła pierwszy meldunek `FCCU_STATE` na CAN i wpisuje informację startową do logów.

#### Blok C: Początek głównej pętli `while (1)` – Watchdog i odbiór zdarzeń

```
    uint32_t last_1s_tick_ms = boot_ms;
    fccu_state_t prev_state = boot_state;
    fccu_event_t evt;

    while (1) {
        fccu_watchdog_feed();

        if (k_msgq_get(&fccu_event_q, &evt, K_MSEC(50)) == 0) {
            handle_event(&evt);
        }

        uint32_t now_ms = k_uptime_get_32();
        fccu_state_t curr_state = fccu_state_step(&state_ctx, now_ms);

```

Przed pętlą zapisywane są zmienne pomocnicze: znacznik czasu ostatniej wysyłki 1-sekundowej (`last_1s_tick_ms`), poprzedni stan (`prev_state`) oraz bufor na zdarzenie (`evt`). W każdym obrocie pętli:

1. Resetowany jest licznik watchdoga (`fccu_watchdog_feed()`).

2. `k_msgq_get(..., K_MSEC(50))` wyjmuje zdarzenie z kolejki. Jeśli coś czekało — zwraca `0` natychmiast i wywołuje `handle_event(&evt)`. Jeśli kolejka była pusta — usypia pętlę maksymalnie na $50\text{ ms}$, żeby nie obciążać procesora.

3. Pobierany jest aktualny czas `now_ms` i przeliczany aktualny stan maszyny stanów `curr_state`.

#### Blok D: Krok 1 w pętli – Reakcja na zmianę stanu (`prev_state != curr_state`)

```
        if (prev_state != curr_state) {
            if (curr_state == STATE_RUNNING) {
                fccu_valves_sync_purge_timer(&valves_ctx, now_ms);
                fccu_fan_resume(&fan_ctx);
                fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
                fccu_actuators_set_fan_duty(fan_ctx.duty);
            } else {
                fccu_valves_reset_outputs(&valves_ctx);
                fccu_fan_stop(&fan_ctx);
                fccu_actuators_emergency_stop();
            }
            fccu_can_send_state((uint8_t)curr_state, valves_ctx.main_valve,
                                valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            prev_state = curr_state;
        }

```

Ten blok uruchamia się wyłącznie w momencie, gdy sterownik zmienił swój stan (np. z `INIT` na `RUNNING`, albo z `RUNNING` na `STOPPED` po utracie łączności z MCU).

* Przy wejściu w `STATE_RUNNING`: synchronizuje stoper przedmuchu, wznawia wentylator i ustawia jego PWM.

* Przy wyjściu do dowolnego innego stanu: natychmiast resetuje zawory, zatrzymuje wentylator i odpala `fccu_actuators_emergency_stop()`.

* Zawsze informuje resztę pojazdu ramką `FCCU_STATE` i zapamiętuje nowy stan w `prev_state`.

#### Blok E: Krok 2 w pętli – Ciągłe egzekwowanie wyjść i automatyczny przedmuch

```
        if (curr_state == STATE_STOPPED || curr_state == STATE_INIT) {
            fccu_valves_reset_outputs(&valves_ctx);
            fccu_fan_stop(&fan_ctx);
            fccu_actuators_emergency_stop();
        } else if (curr_state == STATE_RUNNING) {
            fccu_actuators_set_main_valve(valves_ctx.main_valve);

            int32_t mcu_fc_v_mv = 0;
            bool mcu_fc_v_valid = fccu_can_get_mcu_fc_voltage_mv(&mcu_fc_v_mv, now_ms);

            if (fccu_valves_check_auto_purge(&valves_ctx, true, now_ms, mcu_fc_v_valid, mcu_fc_v_mv)) {
                fccu_actuators_set_purge_valve(1U);
                fccu_actuators_schedule_purge_close(valves_ctx.purge_duration_ms);
                fccu_can_send_state((uint8_t)curr_state, valves_ctx.main_valve,
                                    valves_ctx.purge_valve, fan_ctx.state, fan_ctx.duty);
            }

            fccu_fan_step(&fan_ctx, true, get_fan_temp_c());
            fccu_actuators_set_fan_duty(fan_ctx.duty);
        }

```

Ten blok działa w każdym obrocie pętli (co najmniej 20 razy na sekundę):

* W stanach bezpiecznych (`STOPPED`, `INIT`) nieustannie upewnia się, że wszystkie wyjścia pozostają zamknięte i wyłączone.

* W stanie `RUNNING`:

  1. Utrzymuje właściwy stan zaworu głównego.

  2. Pobiera z warstwy CAN ostatnie napięcie ogniwa przysłane przez MCU (`mcu_fc_v_mv`) wraz z informacją, czy pomiar jest świeży (`mcu_fc_v_valid`).

  3. Pyta logikę zaworów (`fccu_valves_check_auto_purge`), czy pora na automatyczny przedmuch (czasowy lub napięciowy). Jeśli tak — otwiera zawór przedmuchowy, planuje jego zamknięcie po `purge_duration_ms` i od razu raportuje to na CAN.

  4. Przelicza regulator wentylatora (`fccu_fan_step`) i aktualizuje sygnał PWM (`fccu_actuators_set_fan_duty`).

#### Blok F: Krok 3 w pętli – Zadania okresowe co 1 sekundę (czujniki, logi, telemetria)

```
        if ((now_ms - last_1s_tick_ms) >= 1000U) {
            last_1s_tick_ms = now_ms;

            fccu_sensors_read_all(&sensors_data);

#if FCCU_USE_FC_TEMP
            LOG_INF("FC temp: %d mV -> %d.%02d C",
                    sensors_data.fc_temp_mv,
                    sensors_data.fc_temp_x100 / 100,
                    (sensors_data.fc_temp_x100 < 0 ? -sensors_data.fc_temp_x100 : sensors_data.fc_temp_x100) % 100);
#endif

            fccu_flow_snapshot_t flow_snap;
            fccu_sensors_get_flow_snapshot(&flow_snap);
            fccu_flow_update_fast(&flow_ctx, flow_snap.effective_period_us, flow_snap.total_pulses);

            fccu_telemetry_t tel = {
                    .state                 = curr_state,
                    .main_valve            = valves_ctx.main_valve,
                    .purge_valve           = valves_ctx.purge_valve,
                    .fan_state             = fan_ctx.state,
                    .fan_duty              = fan_ctx.duty,
                    .total_volume_x1000    = flow_ctx.total_volume_x1000,
                    .lp_pressure_x100      = (sensors_data.lp_pressure_mv > 0) ? (uint16_t)sensors_data.lp_pressure_mv : 0U,
                    .hp_pressure_x100      = (sensors_data.hp_pressure_mv > 0) ? (uint16_t)sensors_data.hp_pressure_mv : 0U,
                    .fc_voltage_x100       = (int16_t)sensors_data.fc_voltage_mv,
                    .fc_current_x100       = (int16_t)sensors_data.fc_current_mv,
                    .fc_temp_x100          = sensors_data.fc_temp_x100,
                    .ambient_temp_x100     = sensors_data.ambient_temp_x100,
                    .ambient_humidity_x100 = sensors_data.ambient_humidity_x100,
                    .ambient_pressure_x100 = sensors_data.ambient_pressure_x100
            };
            fccu_can_send_telemetry(&tel);
        }

```

W tym bloku kryje się kilka bardzo ważnych detali programistycznych:

1. **Odejmowanie czasu bez znaku `(now_ms - last_1s_tick_ms) >= 1000U`:** nigdy nie używamy dodawania w stylu `now_ms >= next_tick`, bo po ok. $49{,}7$ dniach licznik `uint32_t` przekręca się do zera. Odejmowanie w arytmetyce bez znaku działa matematycznie bezbłędnie nawet w sekundzie przekręcenia licznika.

2. **Sprytne drukowanie temperatury bez użycia `float`:** zamiast włączać ciężką obsługę liczb zmiennoprzecinkowych w `printf`/`LOG_INF`, liczba całkowita `fc_temp_x100` (np. `-1205` dla $-12{,}05^\circ\text{C}$) jest rozbijana na część całkowitą (`fc_temp_x100 / 100` $\rightarrow$ `-12`) oraz część ułamkową (`% 100`). Ponieważ w C reszta z dzielenia liczby ujemnej też jest ujemna (wyszłoby `-12.-05`), zapis `(x < 0 ? -x : x) % 100` bierze wartość bezwzględną, a format `%02d` dopisuje zero wiodące (z `5` robi `05`), dając idealny napis `-12.05 C`.

3. **Aktualizacja całkowitego zużycia wodoru:** pobiera migawkę z łączną liczbą impulsów `total_pulses` i aktualizuje `flow_ctx.total_volume_x1000`.

4. **Bezpieczne pakowanie ciśnień w strukturze `tel`:** wyrażenie `(sensors_data.lp_pressure_mv > 0) ? (uint16_t)sensors_data.lp_pressure_mv : 0U` chroni przed zamianą drobnego ujemnego szumu ADC (np. `-2 mV`) na typ bez znaku `uint16_t`, co dałoby przekręcony wynik `65534` (ponad $655\text{ bar}$!).

5. **Wysyłka pełnej telemetrii:** `fccu_can_send_telemetry(&tel)` rozsyła przygotowaną paczkę jako serię ramek CAN (`FCCU_STATE`, `FCCU_HYDROGEN`, `FCCU_POWER`, `FCCU_ENVIRONMENT`, `FCCU_HYDROGEN_TOTAL` itp.).

## 3. Przewodnik po wszystkich pozostałych plikach i ich funkcjach

Poniżej znajdziesz dokładne omówienie każdego pozostałego pliku w projekcie — za co odpowiada, jakie dane przechowuje i co robi każda wywoływana z niego funkcja.

### 3.1. Konfiguracja i zdarzenia (`src/fccu_config.h`, `src/fccu_events.h`)

* **`src/fccu_config.h`** — centralna tablica stałych konfiguracyjnych kompilacji. Pozwala zmienić zachowanie sterownika bez grzebania w logice:

  * `FCCU_USE_FC_TEMP` — przełącznik (`1` = temperatura z termistora na ogniwie `GPIO10`, `0` = temperatura otoczenia z BME/BMP280).

  * `FCCU_FLOW_FAST_PERIOD_MS` — okres w milisekundach, co ile wątek `flow_fast_thread_fn` wysyła szybką ramkę chwilowego przepływu `0x507` (np. $10\text{ ms}$ dla $100\text{ Hz}$).

  * Stałe sprzętowe i domyślne wartości (np. timeout łączności CAN, progi i domyślne interwały przedmuchu, parametry dzielnika NTC oraz przelicznik impulsów przepływomierza).

* **`src/fccu_events.h`** — definiuje typy komunikatów przesyłanych przez kolejkę `fccu_event_q`:

  * Enum `fccu_event_type_t` zawierający wszystkie 9 rodzajów zdarzeń obsługiwanych w `handle_event()`.

  * Strukturę `fccu_event_t` złożoną z typu zdarzenia (`type`), czasu jego wystąpienia (`timestamp_ms`) oraz unii `data` (oszczędzającej pamięć RAM, bo w jednej chwili w zdarzeniu siedzi tylko jeden typ danych: `u8_val` dla prostych komend 0/1, `fan_cmd` z polami `on`/`duty`, `fan_settings` z `mode`/`p_gain`/`i_gain`/`d_gain` albo `valve_settings` z `interval_s`/`duration_ms`/`has_voltage_cfg`/`mode`/`voltage_threshold_mv`/`voltage_cooldown_s`).

### 3.2. Warstwa logiki (`src/core/`)

Pliki w katalogu `core/` zawierają **czystą logikę i matematykę** — nie dotykają bezpośrednio pinów ESP32 ani rejestrów CAN. Dzięki temu są łatwe do zrozumienia i przetestowania.

#### `src/core/fccu_state.h` i `fccu_state.c` – Maszyna stanów

Przechowuje kontekst `fccu_state_ctx_t` (m.in. aktualny stan `state`, flagę zezwolenia oraz czas ostatniej odebranej ramki CAN).

* **`fccu_state_init(&state_ctx)`** — ustawia bezpieczny stan początkowy i zeruje liczniki timeoutów przy starcie programu.

* **`fccu_state_set_enable(&state_ctx, enable, now_ms)`** — reaguje na ramkę `FCCU_SYSTEM_ENABLE` (`0x510`), zapamiętując, czy nadrzędny system (MCU / operator) zezwolił na pracę ogniwa, oraz odświeża czas kontaktu CAN.

* **`fccu_state_notify_can_rx(&state_ctx, now_ms)`** — odświeża znacznik czasu ostatniej odebranej komendy CAN (tzw. *heartbeat*), aby system wiedział, że połączenie z pojazdem żyje.

* **`fccu_state_step(&state_ctx, now_ms)`** — serce maszyny stanów: sprawdza zezwolenie, pilnuje ewentualnego timeoutu utraty łączności CAN i zwraca aktualny obowiązujący stan (`STATE_INIT`, `STATE_STARTING`, `STATE_RUNNING` lub `STATE_STOPPED`).

#### `src/core/fccu_valves.h` i `fccu_valves.c` – Logika zaworu głównego i przedmuchu

Przechowuje kontekst `fccu_valves_ctx_t` (`main_valve`, `purge_valve`, `purge_mode`, `purge_interval_s`, `purge_duration_ms`, `purge_voltage_threshold_mv`, `purge_voltage_cooldown_s` oraz wewnętrzne znaczniki czasu ostatniego przedmuchu).

* **`fccu_valves_init(&valves_ctx)`** — zamyka oba zawory w logice i ładuje bezpieczne domyślne parametry przedmuchu.

* **`fccu_valves_set_main(&valves_ctx, open)`** — zapisuje żądanie otwarcia (`1`) lub zamknięcia (`0`) zaworu głównego.

* **`fccu_valves_toggle_main(&valves_ctx)`** — odwraca stan zaworu głównego (używane przez pojedyncze kliknięcie przycisku na płytce).

* **`fccu_valves_sync_purge_timer(&valves_ctx, now_ms)`** — wyrównuje stoper automatycznego przedmuchu do chwili bieżącej `now_ms` (wywoływane przy starcie `RUNNING` oraz otwarciu zaworu głównego).

* **`fccu_valves_start_purge(&valves_ctx, is_running, now_ms)`** — weryfikuje warunki bezpieczeństwa (czy stan to `RUNNING` i czy zawór nie jest już otwarty); jeśli wszystko się zgadza, oznacza `purge_valve = 1`, zapisuje czas startu i zwraca `true`.

* **`fccu_valves_close_purge(&valves_ctx)`** — ustawia logiczny stan zaworu przedmuchowego z powrotem na zamknięty (`0`).

* **`fccu_valves_set_settings(...)`** — zapisuje nowe nastawy przedmuchu (przycinając wartości do bezpiecznych granic, np. czas trwania $10\text{--}500\text{ ms}$). Jeśli flaga `has_voltage_cfg` to `true`, aktualizuje również tryb (`TIME`/`VOLTAGE`/`BOTH`), próg napięciowy w $\text{mV}$ i czas odpoczynku (*cooldown*).

* **`fccu_valves_check_auto_purge(&valves_ctx, is_running, now_ms, mcu_v_valid, mcu_v_mv)`** — wywoływana w pętli głównej; sprawdza:

  * w trybie `TIME` (lub `BOTH`), czy od ostatniego przedmuchu upłynęło `purge_interval_s` sekund (przy otwartym zaworze głównym),

  * w trybie `VOLTAGE` (lub `BOTH`), czy pomiar napięcia z MCU jest aktualny (`mcu_v_valid`), czy napięcie `mcu_v_mv` spadło poniżej `purge_voltage_threshold_mv` oraz czy minął czas `purge_voltage_cooldown_s`.
    Jeśli którykolwiek z dozwolonych warunków jest spełniony, ustawia `purge_valve = 1`, aktualizuje znacznik czasu i zwraca `true`.

* **`fccu_valves_reset_outputs(&valves_ctx)`** — zeruje logiczne stany obu zaworów przy przejściu w stan bezpieczny (`STOPPED`/`INIT`).

#### `src/core/fccu_fan.h` i `fccu_fan.c` – Logika wentylatora i regulator PID

Przechowuje kontekst `fccu_fan_ctx_t` (`state`, `duty`, `mode`, `manual_state`, `manual_duty`, `p_gain`, `i_gain`, `d_gain` oraz pamięć błędu dla części całkującej i różniczkującej PID).

* **`fccu_fan_init(&fan_ctx)`** — ustawia początkowe parametry wentylatora (wyłączony, bezpieczne domyślne nastawy).

* **`fccu_fan_set_manual_duty(&fan_ctx, on, duty)`** — zapisuje ręczne włączenie/wyłączenie i żądane wypełnienie PWM (z ograniczeniem do $0\text{--}100\%$).

* **`fccu_fan_set_settings(&fan_ctx, mode, p, i, d)`** — zmienia tryb pracy (`MANUAL`, `LINEAR_TEMP`, `PID`) oraz współczynniki regulatora.

* **`fccu_fan_resume(&fan_ctx)`** — przywraca gotowość pracy wentylatora (np. po wczytaniu z Flash lub wejściu w stan `RUNNING`) na podstawie zapamiętanych ustawień.

* **`fccu_fan_stop(&fan_ctx)`** — zeruje wyjście `duty = 0` i `state = 0` podczas postoju lub awarii, **nie kasując** przy tym zapamiętanych ustawień użytkownika (`manual_duty`, nastaw PID).

* **`fccu_fan_step(&fan_ctx, is_running, temp_c)`** — oblicza aktualne wyjściowe wypełnienie `fan_ctx.duty` (w trybie `MANUAL` bierze `manual_duty`, w `LINEAR_TEMP` wylicza liniowo z temperatury `temp_c`, a w `PID` wykonuje krok regulatora proporcjonalno-całkująco-różniczkującego).

#### `src/core/fccu_flow.h` i `fccu_flow.c` – Przeliczanie przepływu i zużycia wodoru

Przechowuje kontekst `fccu_flow_ctx_t` (m.in. bieżący przepływ i narastającą objętość `total_volume_x1000`).

* **`fccu_flow_init(&flow_ctx)`** — zeruje licznik całkowitej objętości i bieżącego przepływu przy starcie zasilania.

* **`fccu_flow_calc_x1000_from_period_us(effective_period_us)`** — na podstawie czasu między kolejnymi impulsami przepływomierza (w mikrosekundach) wylicza chwilowy przepływ gazu w $\text{Ln/min} \times 1000$. Jeśli impulsy ustały (czas `0` lub przekroczony limit bezruchu), bezpiecznie zwraca `0` (brak dzielenia przez zero!).

* **`fccu_flow_update_fast(&flow_ctx, effective_period_us, total_pulses)`** — aktualizuje bieżący przepływ oraz wylicza całkowitą objętość `total_volume_x1000` bezpośrednio z łącznej liczby impulsów `total_pulses` od startu płytki (co zapobiega narastaniu błędów całkowania w czasie).

### 3.3. Warstwa sprzętowa BSP (`src/bsp/`)

Pliki w katalogu `bsp/` (*Board Support Package*) rozmawiają bezpośrednio ze sterownikami Zephyra i nóżkami ESP32-S3.

#### `src/bsp/fccu_actuators.h` i `fccu_actuators.c` – Fizyczne wyjścia (zawory, PWM wentylatora)

* **`fccu_actuators_init()`** — sprawdza gotowość pinów GPIO dla zaworów i kanału PWM (`ledc0`) dla wentylatora, ustawia je początkowo na `0` (wyłączone) i przygotowuje systemowy timer zamykania przedmuchu.

* **`fccu_actuators_set_main_valve(open)`** — ustawia stan wysoki lub niski na tranzystorze sterującym cewką zaworu głównego.

* **`fccu_actuators_set_purge_valve(open)`** — ustawia stan wysoki lub niski na cewce zaworu przedmuchowego.

* **`fccu_actuators_schedule_purge_close(duration_ms)`** — uruchamia nieblokujący timer systemowy Zephyra (np. `k_timer` lub `k_work_delayable`), który po `duration_ms` wrzuca do kolejki `fccu_event_q` zdarzenie `FCCU_EVT_PURGE_TIMEOUT`.

* **`fccu_actuators_set_fan_duty(duty_percent)`** — przelicza procent ($0\text{--}100\%$) na długość impulsu PWM i wysyła do kontrolera `ledc0`.

* **`fccu_actuators_emergency_stop()`** — **funkcja ostatniej szansy**: natychmiast zamyka oba zawory, anuluje ewentualne zaplanowane akcje i zeruje sygnał PWM wentylatora.

#### `src/bsp/fccu_sensors.h` i `fccu_sensors.c` – Odczyt wszystkich czujników

Wypełnia strukturę `fccu_sensors_data_t` (napięcia i przeliczone wartości: `lp_pressure_mv`, `hp_pressure_mv`, `fc_voltage_mv`, `fc_current_mv`, `fc_temp_mv`, `fc_temp_x100`, `ambient_temp_x100`, `ambient_humidity_x100`, `ambient_pressure_x100`).

* **`fccu_sensors_init()`** — inicjalizuje kanały przetwornika ADC (wewnętrznego ESP32 dla `GPIO10` i zewnętrznego `ADS1015` na `I2C0` pod adresem `0x48`), szuka czujnika `BME/BMP280` pod adresem `0x76` lub `0x77` (jeśli go brak, wypisuje jedynie ostrzeżenie `BME/BMP280 not ready at 0x76 or 0x77`, nie blokując startu całego sterownika) oraz podpina przerwanie sprzętowe GPIO zliczające impulsy z przepływomierza.

* **`fccu_sensors_read_all(&sensors_data)`** — wykonuje kompletny odczyt:

  * czyta napięcie z termistora NTC na `GPIO10` i z krzywej Steinharta-Harta / wzoru Beta przelicza je na `fc_temp_x100` (przy rozwarciu $\approx 3154\text{ mV}$ zwraca $-5000$, czyli $-50{,}00^\circ\text{C}$),

  * czyta kanały z `ADS1015` (ciśnienia, napięcia, prądy),

  * jeśli czujnik `BME/BMP280` jest podłączony i gotowy, pobiera z niego temperaturę, wilgotność i ciśnienie otoczenia.

* **`fccu_sensors_get_flow_snapshot(&snap)`** — bezpiecznie (z chwilową blokadą przerwań lub sekcją krytyczną) kopiuje aktualną liczbę impulsów `total_pulses` oraz efektywny okres między impulsami `effective_period_us` do struktury `fccu_flow_snapshot_t`. Dzięki temu wątek szybki `flow_fast_thread_fn` i główna pętla `main()` zawsze dostają spójne dane, nie „rozrywając” zmiennej w trakcie jej zmiany przez przerwanie.

#### `src/bsp/fccu_button.h` i `fccu_button.c` – Obsługa przycisku na płytce

* **`fccu_button_init()`** — konfiguruje pin przycisku z przerwaniem oraz logiką eliminacji drgań styków (*debouncing*) i rozpoznawania kliknięć. Pojedyncze naciśnięcie wrzuca do kolejki `fccu_event_q` zdarzenie `FCCU_EVT_BTN_SINGLE_CLICK`, a szybkie podwójne — `FCCU_EVT_BTN_DOUBLE_CLICK`.

#### `src/bsp/fccu_storage.h` i `fccu_storage.c` – Pamięć trwała Flash (NVS)

Definiuje strukturę `fccu_persisted_cfg_t` zawierającą wszystkie zapamiętywane pola wentylatora i przedmuchu.

* **`fccu_storage_init()`** — znajduje partycję pamięci Flash dla NVS i montuje w niej system plików (`4 sectors x 4096 B`).

* **`fccu_storage_save(&cfg)`** — zapisuje strukturę konfiguracji (zazwyczaj wraz z numerem wersji / magicznym znacznikiem) pod stałym identyfikatorem klucza w NVS.

* **`fccu_storage_load(&cfg)`** — odczytuje zapisaną konfigurację. Zwraca `true` wyłącznie wtedy, gdy klucz istnieje, a rozmiar i nagłówek zgadzają się z bieżącą wersją struktury; w przeciwnym razie loguje `No saved FCCU config in Flash (using defaults)` i zwraca `false`.

#### `src/bsp/fccu_watchdog.h` i `fccu_watchdog.c` – Strażnik zawieszenia

* **`fccu_watchdog_init()`** — uruchamia sprzętowy zegar Watchdog z limitem $2000\text{ ms}$ (`Watchdog initialized (2000 ms)`).

* **`fccu_watchdog_feed()`** — resetuje odliczanie Watchdoga. Jeśli funkcja nie zostanie wywołana przez $2\text{ s}$, ESP32-S3 wykona twardy reset.

#### `src/bsp/system_status.h` i `system_status.c` – Sygnalizacja stanu

* **`init_system_status()`** — przygotowuje sygnalizację wizualną na płytce (np. diodę LED statusu) na samym początku `main()`.

### 3.4. Warstwa komunikacji CAN (`src/comm/fccu_can.h` i `fccu_can.c`)

To jedyny moduł w projekcie, który bezpośrednio korzysta z wygenerowanych plików `.h` i `.c` ze wspólnego repozytorium organizacji **`lib-can-definitions`**. Przechowuje też strukturę `fccu_telemetry_t`, w której `main.c` przekazuje wszystkie pomiary do wysłania.

* **`fccu_can_init()`** — uruchamia sprzętowy kontroler CAN (TWAI w ESP32-S3), ustawia odpowiednią prędkość transmisji (bitrate), rejestruje filtry sprzętowe dla wszystkich ramek odbieranych przez FCCU (`0x510`–`0x513`, `0x520`–`0x521` oraz `0x104` z MCU) i podpina funkcje odbiorcze (callbacki RX).

* **Funkcje odbiorcze (callbacki RX wewnątrz `fccu_can.c`)** — uruchamiają się w tle przy nadejściu ramki CAN:

  * używają funkcji `..._unpack()` z biblioteki `lib-can-definitions` do wyjęcia sygnałów z surowych bajtów,

  * dla komend (`0x510`, `0x511`, `0x512`, `0x513`, `0x520`, `0x521`) wypełniają strukturę `fccu_event_t` i wrzucają ją do kolejki `fccu_event_q` przez `k_msgq_put()`,

  * dla ramki `MCU_ANALOG_FUEL_CELL` (`0x104`) zapamiętują w bezpiecznej zmiennej przeliczone napięcie ogniwa w miliwoltach (pamiętając o skali $0{,}002\text{ V} \rightarrow \text{mnożenie } \times 2$) oraz czas odbioru tej ramki.

* **`fccu_can_get_mcu_fc_voltage_mv(&out_mv, now_ms)`** — zwraca ostatnie napięcie ogniwa otrzymane z MCU i potwierdza (`true`/`false`), czy ramka przyszła wystarczająco niedawno, by pomiar był wciąż aktualny.

* **`fccu_can_send_state(state, main_valve, purge_valve, fan_state, fan_duty)`** — pakuje i natychmiast wysyła 2-bajtową ramkę `FCCU_STATE` (`0x500`).

* **`fccu_can_send_flow_fast(flow_x1000)`** — pakuje i wysyła 4-bajtową ramkę `FCCU_HYDROGEN_FLOW` (`0x507`) ze skalą $0{,}001\text{ Ln/min}$ (wywoływana z osobnego wątku `flow_fast_thread_fn`).

* **`fccu_can_send_telemetry(&tel)`** — pakuje i wysyła co sekundę zestaw ramek telemetrycznych: `FCCU_STATE` (`0x500`), `FCCU_HYDROGEN` (`0x502`, 6 B), `FCCU_POWER` (`0x503`), `FCCU_ENVIRONMENT` (`0x504`), `FCCU_HYDROGEN_TOTAL` (`0x506`, 4 B ze skalą $0{,}001\text{ Ln}$) oraz ewentualnie `FCCU_CURRENTS` / `FCCU_VERSION`.

### 3.5. Pliki opisujące płytkę i kompilację (`boards/` oraz `CMakeLists.txt`)

* **`boards/espressif/fccu_v2/fccu_v2_procpu.dts`** — główna mapa urządzeń na płytce:

  * Blok **`&i2c0`**: ma `status = "okay";`, korzysta z grupy pinów `i2c0_default`, a w środku ma zadeklarowane podpięte układy: `ads1015@48` oraz węzły czujnika `BME/BMP280` pod adresami `0x76` i `0x77`.

  * Blok **`&i2c1`**: ustawiony na `status = "disabled";` (testy potwierdziły, że na `GPIO4`/`GPIO5` nie ma magistrali I2C z pull-upami).

  * Blok **`&ledc0`**: włącza sprzętowy generator PWM korzystający z grupy pinów `ledc0_default`.

* **`boards/espressif/fccu_v2/fccu_v2-pinctrl.dtsi`** — przypisanie fizycznych nóżek ESP32-S3:

  * Blok **`i2c0_default`**: przypisuje **`I2C0_SDA_GPIO35`** oraz **`I2C0_SCL_GPIO34`**. Nie wolno zamieniać tych pinów miejscami — ich poprawność potwierdza obecność wlutowanego przetwornika `ADS1015` na adresie `0x48`.

  * Blok **`ledc0_default`**: musi znajdować się **wewnątrz** klamry `&pinctrl { ... };` i zawierać kanały PWM (m.in. `LEDC_CH0_GPIO37`, `LEDC_CH1_GPIO17`). Usunięcie całej etykiety `ledc0_default` wywoła błąd kompilacji `undefined node label 'ledc0_default'`.

* **`CMakeLists.txt`** — lista plików źródłowych projektu oraz podpięcie wspólnego repozytorium organizacji `lib-can-definitions` (z wygenerowanymi plikami `.h` i `.c`):

```
set(CAN_DEFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/lib-can-definitions")

target_include_directories(app PRIVATE
    "${CAN_DEFS_DIR}/include"
)

target_sources(app PRIVATE
    "${CAN_DEFS_DIR}/src/<nazwa_wygenerowanego_pliku>.c"
)

```



Opór termistora **NTC** maleje wraz ze wzrostem temperatury. Gdy termistor jest odpięty ($R_{\text{NTC}} \rightarrow \infty$), na wejściu pojawia się niemal pełne napięcie zasilania ($\approx 3154\text{ mV}$), co program celowo przelicza na dolny limit $-50{,}00^\circ\text{C}$ jako czytelny znak przerwy w obwodzie. Po podpięciu sprawnego termistora NTC $1\text{ k}\Omega$ w temperaturze pokojowej ($25^\circ\text{C}$) napięcie spada do około $300\text{ mV}$, a odczyt zaczyna pokazywać realną temperaturę ogniwa.

## 4. Kompletna mapa ramek CAN (po aktualizacji DBC)

Wszystkie sterowniki w pojeździe (**MCU, SWU, LCU, FCCU, TCU, EDU, CCU**) korzystają ze wspólnego pliku **`.dbc`** w repozytorium `lib-can-definitions` (29-bitowe identyfikatory rozszerzone).

### Ramki WYSYŁANE przez FCCU:

| Nazwa ramki | ID Hex (DBC Extended) | Rozmiar | Sygnały, skale i znaczenie | 
| ----- | ----- | ----- | ----- | 
| `FCCU_STATE` | `0x500` (`2147484928`) | $2\text{ B}$ | `STATE` (0=INIT, 1=STARTING, 2=RUNNING, 3=STOPPED), `MAIN_VALVE` (0/1), `PURGE_VALVE` (0/1), `FAN_STATE` (0/1), `FAN_DUTY` ($0\text{--}100\%$). | 
| `FCCU_VERSION` | `0x501` (`2147484929`) | $8\text{ B}$ | Wersja firmware (`MAJOR`, `MINOR`, `PATCH`), `BUILD_ID`, `VEHICLE_TYPE`, `FUELCELL_TYPE`. | 
| `FCCU_HYDROGEN` | `0x502` (`2147484930`) | $6\text{ B}$ | `H2_LP_PRESSURE` ($0{,}01\text{ bar}$), `H2_HP_PRESSURE` ($0{,}01\text{ bar}$), `LEAKAGE_VOLTAGE` ($0{,}01\text{ V}$). | 
| `FCCU_POWER` | `0x503` (`2147484931`) | $8\text{ B}$ | `FC_VOLTAGE` ($0{,}01\text{ V}$), `FC_CURRENT` ($0{,}01\text{ A}$), `DCDC_VOLTAGE` ($0{,}01\text{ V}$), `DCDC_CURRENT` ($0{,}01\text{ A}$). | 
| `FCCU_ENVIRONMENT` | `0x504` (`2147484932`) | $8\text{ B}$ | `FC_TEMP_C` ($0{,}01^\circ\text{C}$), `AMBIENT_TEMP_C` ($0{,}01^\circ\text{C}$), `AMBIENT_HUMIDITY` ($0{,}01\%$), `AMBIENT_PRESSURE` ($0{,}01\text{ hPa}$). | 
| `FCCU_CURRENTS` | `0x505` (`2147484933`) | $8\text{ B}$ | `LOAD_CURRENT`, `HO_CURRENT_1`, `HO_CURRENT_2`, `HO_CURRENT_3` (wszystkie $0{,}01\text{ A}$). | 
| `FCCU_HYDROGEN_TOTAL` | `0x506` (`2147484934`) | $4\text{ B}$ | `H2_TOTAL_VOLUME` (32-bit, skala $0{,}001\text{ Ln}$) — łączne zużycie od startu zasilania (nie zeruje się przy przedmuchu). | 
| `FCCU_HYDROGEN_FLOW` | `0x507` (`2147484935`) | $4\text{ B}$ | `H2_FLOW` (32-bit, skala $0{,}001\text{ Ln/min}$) — szybki przepływ chwilowy wysyłany przez osobny wątek. | 

### Ramki ODBIERANE przez FCCU:

| Nazwa ramki | ID Hex (DBC Extended) | Rozmiar | Sygnały i znaczenie | 
| ----- | ----- | ----- | ----- | 
| `FCCU_SYSTEM_ENABLE` | `0x510` (`2147484944`) | $1\text{ B}$ | `CMD_SYSTEM_ENABLE` (0 = wyłącz, 1 = włącz). | 
| `FCCU_MAIN_VALVE_COMMAND` | `0x511` (`2147484945`) | $1\text{ B}$ | `CMD_MAIN_VALVE` (0 = zamknij, 1 = otwórz). | 
| `FCCU_MANUAL_PURGE` | `0x512` (`2147484946`) | $1\text{ B}$ | `CMD_MANUAL_PURGE` (1 = wywołaj ręczny przedmuch). | 
| `FCCU_FAN` | `0x513` (`2147484947`) | $2\text{ B}$ | `CMD_FAN_ON` (0/1), `CMD_FAN_DUTY` ($0\text{--}100\%$). Zapisywane we Flash. | 
| `FCCU_FAN_SETTINGS` | `0x520` (`2147484960`) | $8\text{ B}$ | `FAN_MODE` (0=MANUAL, 1=LINEAR_TEMP, 2=PID), `P_GAIN`, `I_GAIN`, `D_GAIN` ($0\text{--}1000$). Zapisywane we Flash. | 
| `FCCU_VALVE_SETTINGS` | `0x521` (`2147484961`) | $8\text{ B}$ | `PURGE_INTERVAL_S` ($-1\text{--}16383\text{ s}$), `PURGE_DURATION_MS` ($10\text{--}500\text{ ms}$), `PURGE_MODE` (0=TIME, 1=VOLTAGE, 2=BOTH), `PURGE_VOLTAGE_THRESHOLD_MV` ($0\text{--}60000\text{ mV}$), `PURGE_VOLTAGE_COOLDOWN_S` ($1\text{--}255\text{ s}$). Zapisywane we Flash. | 
| `MCU_ANALOG_FUEL_CELL` | `0x104` (`2147483908`) | $8\text{ B}$ | Wysyłana przez **MCU**. Zawiera sygnał `FUEL_CELL_OUTPUT_VOLTAGE` dla przedmuchu napięciowego. | 

> **Krytyczna uwaga o skali napięcia z MCU (`0x104`):**
> W połączonym DBC organizacji sygnał `MCU_ANALOG_FUEL_CELL.FUEL_CELL_OUTPUT_VOLTAGE` ma skalę $0{,}002\text{ V}$ (czyli $1\text{ LSB} = 2\text{ mV}$). W pliku `src/comm/fccu_can.c` surową wartość rozpakowaną z tej ramki należy zawsze mnożyć przez `2`:
>
> ```
> int32_t voltage_mv = (int32_t)msg.fuel_cell_output_voltage * 2;
> 
> ```
>
> Bez mnożenia przez `2` sterownik widziałby napięcie o połowę niższe niż rzeczywiste i niepotrzebnie otwierałby zawór przedmuchowy.

## 

## 5. Krótki słowniczek pojęć

* **ADC** – przetwornik analogowo-cyfrowy; zamienia napięcie elektryczne (np. z termistora) na liczbę odczytywaną przez program.

* **ADS1015** – wlutowany na płytce `fccu_v2` dokładny przetwornik napięcia na magistrali `I2C0` (adres `0x48`).

* **BME280 / BMP280** – czujnik temperatury, ciśnienia (i wilgotności w BME) podłączany do `I2C0` (adres `0x76` lub `0x77`).

* **BSP (*Board Support Package*)** – warstwa kodu obsługująca bezpośrednio sprzęt konkretnej płytki.

* **CAN** – dwuprzewodowa sieć komunikacyjna łącząca wszystkie sterowniki w pojeździe.

* **Cooldown** – minimalny czas odpoczynku (w sekundach) między kolejnymi przedmuchami napięciowymi.

* **DBC** – plik będący słownikiem magistrali CAN (opisy ramek, bitów, skal i jednostek).

* **Devicetree (`.dts` / `.dtsi`)** – pliki konfiguracji sprzętu w Zephyrze opisujące połączenia pinów na płytce.

* **Duty cycle (wypełnienie PWM)** – procent czasu ($0\text{--}100\%$), przez jaki zasilanie wentylatora jest włączone.

* **Fail-safe** – zasada projektowania sprawiająca, że w razie awarii układ przechodzi w najbezpieczniejszy stan (zamknięcie zaworów).

* **Flash / NVS** – pamięć trwała wewnątrz mikrokontrolera, przechowująca ustawienia bez zasilania.

* **GPIO** – uniwersalne wyprowadzenie (nóżka) mikrokontrolera.

* **I2C** – dwuprzewodowa magistrala (`SDA` i `SCL`) łącząca układy scalone po ich adresach.

* **Kolejka zdarzeń (`k_msgq`)** – bezpieczny bufor przekazujący komunikaty z przerwań i timerów do głównej pętli programu.

* **Maszyna stanów** – organizacja programu oparta na ściśle zdefiniowanych trybach pracy (`INIT`, `STARTING`, `RUNNING`, `STOPPED`).

* **NTC** – termistor, którego rezystancja maleje wraz ze wzrostem temperatury.

* **PID** – algorytm automatycznej regulacji temperatury wentylatorem za pomocą nastaw `P`, `I`, `D`.

* **Purge (przedmuch)** – krótkotrwałe otwarcie zaworu w celu usunięcia nagromadzonej wody i zanieczyszczeń z ogniwa.

* **PWM** – szybkie impulsowe sterowanie zasilaniem pozwalające płynnie regulować obroty wentylatora.

* **Submoduł Git** – sposób osadzenia wspólnego repozytorium (`lib-can-definitions`) wewnątrz projektu z kontrolą wersji.

* **Watchdog** – sprzętowy zegar bezpieczeństwa resetujący mikrokontroler w razie zawieszenia się programu.

* **Zephyr RTOS** – system operacyjny czasu rzeczywistego zarządzający wątkami, czasem i sterownikami na ESP32-S3.
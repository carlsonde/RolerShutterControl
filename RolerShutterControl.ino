/*
    -----------------------------------------------
   Rolladensteuerung V 1.0 von Andreas Stockburger
   V1.1 Portierung und Modifizierung Dirk Allmenidnger
   -----------------------------------------------

   Diese Rolladensteuerung steuert 7 Gruppen von Rolladen mit 7 Rolladentastern (12 Taster - je 2 f¸r einen Rolladen). Der 7. Rolladen wird nur ferngesteuert.

   Ausgehend von den jeweils gedr¸ckten Tasten werden die Relais bedient. Es wird pro Rolladen ein Richtungsrelais und
   ein Schaltrelais bedient. Die Relais werden bestmˆglich "geschont" es wird immer zuerst ds Richtungsrelais bedient.
   Mit einer Zeitverzˆgerung wird das Schaltrelais gesschalten.

   Tastendr¸cke kˆnnen durch reale Tasten (Rolladentaster) aber auch virtuellen Tasten erzeugt werden. Virtuelle Tasten
   werden durch Doppelklick oder zeitgesteuert ausgelˆst. Diese  bestimmen dann die Position der jeweiligen Relais.

   Hardware:
   ---------
   12 Taster sind mit PB0-PB4 und PA0-PA7 verbunden. Die Taster schalten +5V gegen Masse. D.h. Sie sind low-aktiv.
   Ein nicht gedr¸ckter Taster liefert "1", ein gedr¸ckter liefert "0"

   Die Relais h‰ngen an PD3-PD4 und PC0-PC7 und werden gegen Masse geschalten. "1" am Port f¸hrt zu angezogen Relais.

   An PB4 h‰ngt eine Status-LED

   An XTAL1 ist ein 16Mhz Quarz-Oszillator angeschlossen.

   An PD0+PD1 ist die Serielle Schnittstelle mittels MAX232 beschaltet

   Rolladenmapping
   R0: Büro Nord
   R1: Büro West
   R2: WZ West
   R3: WZ Süd Balkontüre
   R4: EZ Süd
   R5: EZ Ost Terassentüre
   R6: Küche

*/

//Defines zur Anpassung an Quarze (Produktionsumgebung 10Mhz)
// #define FR_CPU 16000000UL // 7372800UL // Takt in Herz
// Prescale 64
#define FR_CPU 250000UL
#define T_VALUE 250 //was 1250 // 36864    // (FR_CPU/T_VALUE muss ganzzahlig sein)
#define ISR_VAL3 FR_CPU/T_VALUE-1 // 16000000/64/250=1000 

//Includes
#include <EEPROM.h>

//Variable f¸r Rolladenlaufzeit
uint8_t time_tab_total[16] = {29, 27, 20, 21, 20, 21, 20, 21, 20, 21, 29, 28, 10, 10, 10, 10}; //Default-Werte f¸r Rolladenlaufzeit
uint8_t time_tab_half[16] =  { 4, 20,  4, 14,  4, 13,  4, 13,  4, 14,  6, 21, 10, 10, 10, 10}; //Default-Werte f¸r Rolladenlaufzeit
uint8_t time_tab[16]     = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// position of the rolladen,
// 0 is undefinded, 1 is way up, 2 is up, 4 is way down, 8 is down, 16 is on the way to the middle dir up  32 is in the middle, 64 is on the way to the middle down, 128 is somewhere
volatile uint8_t rol_pos[8]       = {0, 0, 0, 0, 0, 0, 0, 0 };

//Variablen f¸r Eingabe
volatile uint16_t real_keys, real_keys_old, doppel_klick, trippel_klick, real_keys_pressed;
volatile uint16_t klick_timer_tab[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}; // Timer-Speicher f¸r Doppelklick-Zeiten
volatile uint16_t doppel_klick_val = 500; // Default-Wert f¸r doppellick ~1s
volatile uint16_t triklick_timer_tab[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}; // Timer-Speicher f¸r Doppelklick-Zeiten
volatile uint16_t trippel_klick_val = 500; // Default-Wert f¸r doppellick ~1s

//Variablen wg. Zugriff aus untersch. Funktionen
uint16_t output_ist, output_soll; // Zugriff aus main und aus der Kofig heraus
uint8_t template_nr = 3; // Rolle der Steuerung: 0=UG, 1=EG, 2=OG 3=Frei konfigurierbar
//                                   H0     R0      H1      R1      H2      R2      H3      R3      H4      R4      H5       R5      H6      R6      H7     R7
uint16_t template_table[4][16] = { {0xEABE, 0xD57D, 0xFFEB, 0xFFD7, 0xFFEB, 0xFFD7, 0xFABE, 0xF57D, 0xEABE, 0xD57D, 0xEABE, 0xD57D, 0xEABE, 0xD57D, 0xDAAA, 0xE555}, // UG
                                   {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, // EG
                                   {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, // OG
                                   {0xEABE, 0xD57D, 0xFFEB, 0xFFD7, 0xFFEB, 0xFFD7, 0xFABE, 0xF57D, 0xEABE, 0xD57D, 0xEABE, 0xD57D, 0xEABE, 0xD57D, 0xDAAA, 0xE555}
};// User-Tab

// Strings f¸r generelle Verwendung
char string60[] = "1234567890123456789012345678901234567890123456789012345678901";
char string15[] = "1234567890123456";

//Rollanmapping f¸r uhrzeitgesteuerten Betrieb
// 0011223344556677
// RSRSRSRSRSRSRSRS
uint16_t zeitgest_hoch_val = 0xEAAA;      // 1110101010101010
uint16_t zeitgest_runter_val = 0xF555;    // 1111010101010101

//Variablen f¸r die Uhr
typedef struct
{
  uint8_t sec;
  uint8_t min;
  uint8_t hour;
  uint8_t day;
  uint8_t wday;
  uint8_t month;
  uint16_t year;
  uint8_t dls_flag; // 1=Sommerzeit im Okt. bereits zur¸ckgestellt
  uint8_t leapyear;
} datestruct;


volatile datestruct zeit; // enth‰lt die aktuelle Zeit
volatile datestruct hoch_mofr;
volatile datestruct hoch_saso;
volatile datestruct runter_mofr;
volatile datestruct runter_saso; // autom. Schaltzeiten
uint8_t zeitsteuer_enable = 0;

volatile uint16_t int_cnt = 0; //Z‰hler Anzahl der f¸r Interrupts in einer Sekunde
volatile uint16_t quarz_adjustment = 160; // Default-Wert f¸r Quarz-Adjustment (f¸r exakte Sekunde)
uint8_t old_sec;
uint16_t v_keys, v_keys_old , v_keys_timer , zeitgesteuert;
uint8_t key_state[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned long start_time[16], stop_time[16];
int click_time, debounce_time, press_time;
volatile uint16_t single_click =  0; //{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
volatile uint16_t single_press =  0;
uint16_t double_click =  0; //{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
uint16_t trippel_click = 0; //{false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
uint16_t long_click = 0;  // {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
uint16_t long_press = 0; //   {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
uint16_t long_pressed = 0; //   {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
volatile uint16_t both_pressed = 0; //  {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
volatile uint16_t both_double_clicked = 0; //   {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false};
volatile uint16_t double_press;

//Uptime-Varilen
typedef struct
{
  uint8_t sec;
  uint8_t min;
  uint8_t hour;
  uint16_t day;
} uptimestruct;
volatile uptimestruct uptime;

//Variablen f¸r EEPROM-Usage
  uint16_t ee_addr = 0;

void setup() {
  // put your setup code here, to run once:

  // Controller Konigurieren
  DDRA  = 0x00; // PA0-7 Eing‰nge
  DDRC  = 0x00; // PC0-7 Eing‰nge
  DDRK  = 0xff; // PK0-7 Ausg‰nge
  DDRL  = 0xff; // PL0-7 Ausg‰nge
  pinMode(LED_BUILTIN, OUTPUT);


  // initialize serial:
  Serial.begin(115200);

  //Versionsinfo
  //  if (Serial.available()) {      // If anything comes in Serial (USB),
  Serial.write("Version vom: ");   //
  Serial.write(strcpy_P(string60, PSTR(__DATE__)));   //
  Serial.write(" - ");   //
  Serial.write(strcpy_P(string60, PSTR(__TIME__)));   //
  Serial.write("\n");   //

  //    }

  //Timer konfigurieren
  // CPU frequency 16Mhz for Arduino
  // maximum timer counter value (256 for 8bit, 65536 for 16bit timer)
  // Divide CPU frequency through the chosen prescaler (16000000 / 64 = 250000)
  // Divide result through the desired frequency (250000 / 200Hz = 1250)
  // Verify the result against the maximum timer counter value (1250 < 65536 success) if fail, choose bigger prescaler.
  // noInterrupts();           // disable all interrupts

  TCCR3A = 0;
  TCCR3B = 0;
  TCNT3 = 0;
  TCCR3B |= (1 << WGM32); // CTC mode
  //TCCR3B |= (1<<CS32); // set prescaler to 256
  TCCR3B |= (1 << CS30);
  TCCR3B |= (1 << CS31); // set prescaler to 64
  OCR3A = T_VALUE;
  TIMSK3 = (1 << OCIE3A);


  //Variablen initialisieren
  output_ist = 0;
  v_keys = 65535;
  v_keys_old = 0;
  v_keys_timer = 65535; //Initialisierung
  doppel_klick = 0;
  trippel_klick = 0;
  old_sec = zeit.sec;
  zeitgesteuert = 0;
  click_time   =  500;
  debounce_time =  90;
  press_time = 1000;
  both_pressed = 0;
  real_keys_old = 0;
  real_keys = 0;
  single_click = 0;
  double_click =0;
  both_double_clicked = 0;
  double_press = 0;
  //l‰dt gespeicherte Konfig-Werte
  //load_eeprom_konfig();

  //Interupts erlauben
  //  delay(1000);
  interrupts();           // enable all interrupts
}


void toggle_LED(void) // Toggled die LED an PB4
{
  if (PINB & (1 << PINB7)) {
    PORTB &= ~(1 << PB7);
  }
  else {
    PORTB |=  (1 << PB7);
  }
}

void calc_zeit(void) // Berechnet Uhrzeit, Datum, Sommerzeit, Schaltjahr etc.
{
  uint8_t m_end[2][12] = {  {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, // #Tage im Monat
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
  }; // s.o. mit Sommerzeit
  zeit.sec++;
  if (zeit.sec > 59) { // Sekunde
    zeit.sec = 0;
    zeit.min++;
    if (zeit.min > 59) { // Minute
      zeit.min = 0;
      zeit.hour++;
      //Sommerzeit im M‰rz
      if (zeit.month == 3 && zeit.wday == 0 && zeit.day >= 25  && zeit.hour == 2) {
        zeit.hour++;
        zeit.dls_flag = 0;
      }
      //Sommerzeit im Oktober Achtung: Durch R¸ckstellen der Uhr mˆgliche Endlosschleife daher Flag
      if (zeit.month == 10 && zeit.wday == 0 && zeit.day >= 25 && zeit.hour == 3 && zeit.dls_flag == 0) {
        zeit.hour--;
        zeit.dls_flag = 1;
      }
      if (zeit.hour > 23) { // Stunde
        zeit.hour = 0;
        zeit.day++;
        zeit.wday++;
        if (zeit.wday > 6) { // Wochentag
          zeit.wday = 0;
        }
        //Schaltjahr
        if ((zeit.year % 4 == 0) && ((zeit.year % 100 != 0) || (zeit.year % 400 == 0)))
          zeit.leapyear = 1;
        else
          zeit.leapyear = 0;
        //Tag
        if (zeit.day > m_end[zeit.leapyear][zeit.month - 1]) {
          zeit.day = 1;
          zeit.month++;
        }
        //Jahr
        if (zeit.month > 12) {
          zeit.month = 1;
          zeit.year++;
        }
      }
    }
  }
}

void calc_uptime(void) // Berechnet Uptime
{
  uptime.sec++;
  if (uptime.sec > 59) { // Sekunde
    uptime.sec = 0;
    uptime.min++;
    if (uptime.min > 59) { // Minute
      uptime.min = 0;
      uptime.hour++;
      if (uptime.hour > 23) { // Stunde
        uptime.hour = 0;
        uptime.day++;
      }
    }
  }
}


ISR(TIMER3_COMPA_vect) // Interrupt-Routine
{
  uint8_t i;
  uint16_t real_key_store, _real_keys;
  uint8_t _key_state;
  boolean _key;
   
  
  int_cnt++;
  if (int_cnt > ISR_VAL3-1) {
    OCR3A = T_VALUE - quarz_adjustment; // abgleichen auf 1,0000000s
    if (int_cnt > ISR_VAL3) {
      OCR3A = T_VALUE;
      int_cnt = 0;
      calc_zeit();
      calc_uptime();
      toggle_LED();
      //Serial.write("|");
    }
  }
 // if ((int_cnt % 3) == 0) {
    
  
    // Abfragen der Physikalischen Ports
    real_key_store = (((uint16_t)PINA) << 8) | ((uint16_t)PINC); // Erstellung 16-BIT-Zahl abh. von Rolladenasten
    _real_keys = 65535;
//    real_keys = 65535;
    //doppel_klick = 0;
    //single_click = 0;
    //double_click = 0;
    // Doppelklick-Erkennung
    // (1<<i) kann noch vereinfacht werden, wenn in jedem Schleifendurchlauf um 1 geschoben wird ...
    for (i = 0; i < 16; i++) {
      //both if statements switch values of keys (up and down) 01 becomes 10 and 10 becomes 01. 11 and 00 should stay the same. 
      if ((i % 2) == 0) {
        if ((real_key_store & (1 << i)) == 0 ) {
          _real_keys &= ~(1 << (i+1));
        }
        if ((real_key_store & (1 << (i+1))) == 0 ) {
          _real_keys &= ~(1 << i);
        }
      }
      if ((_real_keys & (1 << i)) == 0) _key = true; //key pressed
      else _key = false;
      _key_state = key_state[i];

      switch(_key_state) {
        case 0:
          if (_key) {
            _key_state = 1;
            //Serial.print(" State0 ");
            start_time[i] = millis();
          }
          break;
        case 1:
          //Serial.print(" State1 ");
          if ( (!_key) && ((unsigned long)(millis() - start_time[i]) < debounce_time)) {
            _key_state = 0; //time is in debounce time, restart
          }
          else if (!_key) {
            single_press |= (1 << i);
            _key_state = 2; //key release detected go to state 2;
            stop_time[i] = millis();            
          }
          else if ( (_key) && ((unsigned long)(millis() - start_time[i]) > press_time)) {
            long_press |= (1 << i);
            _key_state = 6;
          }
          break;
        case 2:
          if ( (_key) && ((unsigned long)(millis() - stop_time[i]) > debounce_time)) {
            double_press |= (1 << i);
            _key_state = 3; //hey, cool, key pressed a second time -> going for doubleklick
            Serial.print(" double_press1 ");
            Serial.print(double_press);
             start_time[i] = millis();
          }
          else if (((double_click & (1 << i)) == 0 ) && ((unsigned long)(millis() - stop_time[i]) > click_time)) {
            single_click |= (1 << i); //button pressed an released so one click happened
            single_press &= ~(1 << i);
            //real_keys &= ~(1 << (i));
            _key_state = 0;
            Serial.print(" single_click1 ");
            Serial.print(single_click);
          }
          break;
        case 3:
          Serial.print(" State3 ");
          if ((~_key) && ((unsigned long)(millis() - start_time[i]) > debounce_time)) {
            double_click |= (1 << i);
            double_press &= ~(1 << i);
            _key_state = 0; //key press detected go to state 2;
            Serial.print(" double_click1 ");
            Serial.print(double_click);
            
          }
          
          break;
        case 6:  // stop, when key will be releades after long press
          Serial.print(" State6 ");
          if  (!_key)  {
            long_pressed |= (1 << i);
            long_press &= ~(1 << i);
            _key_state = 0;
          } 
          break;
      }
      key_state[i] = _key_state;
      
      if ((i % 2) == 0) {
        if (( (single_click & (1 << i)) > 0 ) && ( (single_press & (1 << (i+1))) > 0 )) {
          both_pressed |= (1 << i);
          //single_press |= (1 << (i+1));
          single_click &= ~(1 << i);
          single_click &= ~(1 << (i+1)); 
          //real_keys |= (1 << (i));
          //real_keys |= (1 << (i+1));
          Serial.print(both_pressed);
            Serial.print(" single_click2 ");
            Serial.print(single_click);
        } 
        else if (((single_click & (1 << i)) > 0 ) && ((both_pressed & ( 3 << (i))) > 0)) {
            Serial.print(" single_click!2 ");
            Serial.print(single_click);
            single_click &= ~(1 << i);
            //real_keys &= ~(1 << (i));
        }
        if (( (double_click & (1 << i)) > 0 ) && ( (double_press & (1 << (i+1))) > 0 )) {
          both_double_clicked |= (1 << i);
          double_click &= ~(1 << i);
          double_click &= ~(1 << (i+1)); 
          Serial.print(" double_click2 ");
          Serial.print(double_click);
          if (zeitsteuer_enable == 0) zeitsteuer_enable = 1;
          else zeitsteuer_enable = 0;
        } 
        else if (((double_click & (1 << i)) > 0 ) && ((both_double_clicked & ( 3 << (i))) > 0)) {
          Serial.print(" double_click!2 ");
          Serial.print(single_click);
          double_click &= ~(1 << i);
          //real_keys &= ~(1 << (i));
        }
        
      } else {
        if (( (single_click & (1 << i)) > 0 ) && ( (single_press & (1 << (i-1))) > 0 )) {
          both_pressed |= (1 << (i-1));
          //single_press |= (1 << (i));
          single_click &= ~(1 << i);
          single_click &= ~(1 << (i-1));
          //real_keys |= (1 << (i));
          //real_keys |= (1 << (i-1));
          Serial.print(both_pressed);
          Serial.print(" single_click3 ");
          Serial.print(single_click);
        }
        else if (((single_click & (1 << (i))) > 0 ) && ( (both_pressed & (3 << (i-1))) > 0 )) {
            Serial.print(" single_click!3 ");
            Serial.print(single_click);
            single_click &= ~(1 << (i));
            //real_keys &= ~(1 << (i));
        }
        if (( (double_click & (1 << i)) > 0 ) && ( (double_press & (1 << (i-1))) > 0 )) {
          both_double_clicked |= (1 << (i-1));
          double_click &= ~(1 << i);
          double_click &= ~(1 << (i-1)); 
          Serial.print(" double_click4 ");
          Serial.print(double_click);
          if (zeitsteuer_enable == 0) zeitsteuer_enable = 1;
          else zeitsteuer_enable = 0;
        }
        else if (((double_click & (1 << i)) > 0 ) && ((both_double_clicked & ( 3 << (i-1))) > 0)) {
          Serial.print(" double_click!4 ");
          Serial.print(single_click);
          double_click &= ~(1 << i);
          double_click &= ~(1 << (i-1));
          //real_keys &= ~(1 << (i));
        }
        
      }

//      if (double_click[i]) {
//        doppel_klick &= ~(1 << i);
//      }
    }
    real_keys = ~single_click;
    doppel_klick = double_click;
/*    if (real_keys != 65535) {
    Serial.print(" RK ");
    Serial.print(real_keys);
    }
*/
    
/*      if (klick_timer_tab[i] > 0) klick_timer_tab[i]--; // Pr¸ft auf Timer >0 und decremiert; entspricht "Continue"
      else doppel_klick &= ~(1 << i); // lˆscht BIT i in Variable
      if (triklick_timer_tab[i] > 0) triklick_timer_tab[i]--; // Pr¸ft auf Timer >0 und decremiert; entspricht "Continue"
      else trippel_klick &= ~(1 << i); // lˆscht BIT i in Variable

      if (((real_keys & (1 << i)) == 0) && ((real_keys_old & (1 << i)) > 0)) { // Taste wurde gedr¸ckt "fallende Flanke"
        real_keys_pressed = 1; //Variable um im Wizzard den Tastendruck einfach abfragen zu kˆnnen
        if ((( i % 2 ) == 0 ) && ((real_keys & (1 << (i+1))) == 0)) {
          Serial.print("BothKeys\n");
          both_pressed = true;
          real_keys |= (1 << i); 
          real_keys |= (1 << (i+1));
        }
        else if ((( i % 2 ) != 0 ) && ((real_keys & (1 << (i-1))) == 0)) {
                   Serial.print("BothKeys\n");
          both_pressed = true;
          real_keys |= (1 << i); 
          real_keys |= (1 << (i+1));
        }

        if ((triklick_timer_tab[i] > 0) && ((doppel_klick & (1 << i)) > 0)) {
          trippel_klick |= (1 << i); // trippelklick erkannt: Setze BIT i
          doppel_klick &= ~(1 << i); // trioppelklick erkannt: Lösche bei Doppelklick BIT i
          Serial.print("Trippelklick\n");
        }
        else triklick_timer_tab[i] = trippel_klick_val; // setze neue Endzeit f¸r Taster

        if ((klick_timer_tab[i] > 0) && ((trippel_klick & (1 << i)) == 0)){ // falls zeit noch nicht abgelaufen dann doppelklick
          doppel_klick |= (1 << i); // doppelklick erkannt: Setze BIT i
          Serial.print("Doppelklick\n");
        }
        else klick_timer_tab[i] = doppel_klick_val; // setze neue Endzeit f¸r Taster
      }
    }
  }
*/

  real_keys_old = real_keys;
}

void itos(char string[], uint8_t zahl, uint8_t stellen) // Konvertiert int zu ASCII mit Anzahl Stellen und f¸hr. Null
{
  uint8_t zehner = 0;
  uint8_t hunderter = 0;

  while (zahl > 99) {
    zahl -= 100;
    hunderter++;
  }
  while (zahl > 9) {
    zahl -= 10;
    zehner++;
  }
  switch (stellen) {
    case 3:
      string[0] = hunderter + 0x30;
      string[1] = zehner + 0x30;
      string[2] = zahl + 0x30;
      string[3] = '\0';
      break;
    case 2:
      string[0] = zehner + 0x30;
      string[1] = zahl + 0x30;
      string[2] = '\0';
      break;
    case 1:
      string[0] = zahl + 0x30;
      string[1] = '\0';
      break;
  }
}

void ltos(char string[], uint16_t zahl, uint8_t stellen) // Konvertiert long zu ASCII mit Anzahl Stellen und f¸hr. Null
{
  uint8_t zehner = 0;
  uint8_t hunderter = 0;
  uint8_t tausender = 0;
  uint8_t zehntausender = 0;

  while (zahl > 9999) {
    zahl -= 10000;
    zehntausender++;
  }
  while (zahl > 999) {
    zahl -= 1000;
    tausender++;
  }
  while (zahl > 99) {
    zahl -= 100;
    hunderter++;
  }
  while (zahl > 9) {
    zahl -= 10;
    zehner++;
  }
  switch (stellen) {
    case 5:
      string[0] = zehntausender + 0x30;
      string[1] = tausender + 0x30;
      string[2] = hunderter + 0x30;
      string[3] = zehner + 0x30;
      string[4] = zahl + 0x30;
      string[5] = '\0';
      break;
    case 4:
      string[0] = tausender + 0x30;
      string[1] = hunderter + 0x30;
      string[2] = zehner + 0x30;
      string[3] = zahl + 0x30;
      string[4] = '\0';
      break;
    case 3:
      string[0] = hunderter + 0x30;
      string[1] = zehner + 0x30;
      string[2] = zahl + 0x30;
      string[3] = '\0';
      break;
    case 2:
      string[0] = zehner + 0x30;
      string[1] = zahl + 0x30;
      string[2] = '\0';
      break;
    case 1:
      string[0] = zahl + 0x30;
      string[1] = '\0';
      break;
  }
}

void print_nibble(uint16_t inp, uint8_t nibble) // nibble 0,1 oder 2
{
  uint8_t i;
  uint16_t t = 0;

  for (i = 0; i < 4; i++) {
    switch (nibble) {
      case 0:
        t = inp & 0x0008;
        break;
      case 1:
        t = inp & 0x0080;
        break;
      case 2:
        t = inp & 0x0800;
        break;
      case 3:
        t = inp & 0x8000; // wird nicht gebraucht
        break;
    }
    if (t == 0) strcat(string60, "0");
    else strcat(string60, "1");
    inp = inp << 1;
  }
}

void out_time( const volatile datestruct & zeit ) // gibt Zeit in string60 aus
{
  strcpy(string60, "");
  itos(string15, zeit.hour, 2);
  strcat(string60, string15);
  strcat(string60, ":");
  itos(string15, zeit.min, 2);
  strcat(string60, string15);
  strcat(string60, ":");
  itos(string15, zeit.sec, 2);
  strcat(string60, string15);
}

uint8_t decode_sequence(uint8_t neu, uint8_t alt) //Ermittelt Schonstellungen f¸r Relais
{
  /*
    Abh. vom Ist-Zustand und dem zu ereichenden Ziel-Zustand sind bis zu 3 Schaltschritte notwendig
    die Wahrheitstabelle zeigt die logik. Das Richtungsrelais darf nur Stromlos geschalten werden.

    I=Ist-Zustand R=Richtungselais
    Z=Zielzustand S=Schaltrelais
    S1..3= Stati mit jeweils Zeitvz. 100ms

    I  Z     S1   S2   S3  S1-Dezimal
    RS RS    RS   RS   RS
    00 00 -> 00 - 00 - 00  0
    00 01 -> 01 - 01 - 01  1
    00 10 -> 10 - 10 - 10  2
    00 11 -> 10 - 11 - 11  3
    01 00 -> 00 - 00 - 00  0
    01 01 -> 01 - 01 - 01  1
    01 10 -> 00 - 10 - 10  0
    01 11 -> 00 - 10 - 11  0
    10 00 -> 00 - 00 - 00  0
    10 01 -> 00 - 01 - 01  0
    10 10 -> 10 - 10 - 10  2
    10 11 -> 11 - 11 - 11  3
    11 00 -> 10 - 00 - 00  2
    11 01 -> 10 - 00 - 01  2
    11 10 -> 10 - 10 - 10  2
    11 11 -> 11 - 11 - 11  3

    S2 und S3 ergeben sich iterativ aus S1. Daher nur Tab f¸r S1
  */
  uint8_t table[16] = {0, 1, 2, 2, 0, 1, 0, 0, 0, 0, 2, 3, 2, 2, 2, 3}; // Tab f¸r Relais-Schonstellungen
  uint8_t tn, ta, ind, erg, i, j, wert;

  erg = 0;
  for (i = 0; i < 4; i++) {
    tn = neu & 0xc0;
    ta = alt & 0xc0;
    ind = (ta >> 4) + (tn >> 6);
    wert = table[ind];
    for (j = 3; j > i; j--) {
      wert = wert << 2;
    }
    erg = erg | wert;
    neu <<= 2;
    alt <<= 2;
  }
  return (erg);
}

void switch_relais(uint16_t soll , uint16_t ist) //Schaltet Relais
{
  uint8_t tk_soll, tl_soll, tk_ist, tl_ist, i;

  //convert 12-BIT zu 8 und 4 BIT
  tk_soll = (uint8_t)(soll >> 8);
  tl_soll = (uint8_t)(soll & 0xff);
  tk_ist = (uint8_t)(ist >> 8);
  tl_ist = (uint8_t)(ist & 0xff);

  for (i = 0; i < 2; i++)   //DA was 3 but want motors hold instead of reverse driving
  {
    tk_ist = decode_sequence(tk_soll, tk_ist);
    PORTK = tk_ist;
    tl_ist = decode_sequence(tl_soll, tl_ist);
    PORTL = tl_ist;
    if (i < 2) _delay_ms(125); // Schaltverzˆgerung der Relais xxx
  }
}

uint16_t keys2relais(uint16_t input) // mappt 2 Rolladen (4 Taster) auf 4 Relais
{
  /* DA hier codierung ändern runter 11 hoch 01
    Tabelle gilt f¸r 2 Rolladen (4 Relais) und 2 Rolladenschalter (4 Taster).
    Es wird dargestellt, welche Tastenkombinationen zu welchen Relaisstellungen f¸hren

    R=Taster f¸r Runter lowaktiv (0=gedr¸ckt)
    H=Taster f¸r Hoch lowaktiv (0=gedr¸ckt)
         R=Richtungsrelais;
       S=Schaltrelais
    RHRH | RSRS Dezimal   RSRS  Dezimal
    1111 | 0000 0         0000  0
    1110 | 0011 3         0001  1
    1101 | 0001 1         0011  3
    1100 | 0000 0         0000  0
    1011 | 1100 12        0100  4
    1010 | 1111 15        0101  5
    1001 | 1101 13        0111  7
    1000 | 1100 12        0100  4
    0111 | 0100 4         1100  12
    0110 | 0111 7         1101  13
    0101 | 0101 5         1111  15
    0100 | 0100 4         1100  12
    0011 | 0000 0         0000  0
    0010 | 0011 3         0001  1
    0001 | 0001 1         0011  3
    0000 | 0000 0         0000  0
  */

  //  uint8_t table[16] = {0,1,3,0,4,5,7,4,12,13,15,12,0,1,3,0}; // Tab. f¸r Zuordnung Relais zu Tasten
  uint8_t table[16] =   {0, 3, 1, 0, 12, 15, 13, 12, 4, 7, 5, 4, 0, 3, 1, 0}; // Tab. f¸r Zuordnung Relais zu Tasten
  uint16_t t, output;

  t = input & 0xF000;
  t >>= 12;
  output = table[t];
  output <<= 4;

  t = input & 0x0F00;
  t >>= 8;
  output = table[t];
  output <<= 4;

  t = input & 0x00F0;
  t >>= 4;
  output |= table[t];
  output <<= 4;

  t = input & 0x000F;
  output |= table[t];

  return (output);
}

uint16_t map_keys(uint16_t input)
{
  uint16_t i, output;

  output = 65535;
  for (i = 0; i < 16; i++) {
    if ((input & (1 << i)) == 0) output &= template_table[template_nr][i];
  }
  return (output);
}

uint16_t startstop_time(uint16_t keys, uint16_t old_keys, uint16_t old_value) // Setzt Endzeit pro Taster
{
  uint16_t i, new_value;
  boolean key_active = false;
  uint8_t rol_pos_old;
  boolean half_time = false;
  // (1<<i) kann nch vereinfacht werden, wenn in jedem Schleifendurchlauf um 1 geschoben wird ...
  new_value = old_value;
  for (i = 0; i < 16; i++) {
    rol_pos_old = 0;
    if ( rol_pos[(i/2)] >= 128 ) rol_pos_old = (1 << 7);
    if (( both_pressed & (1 << i)) > 0) {
        Serial.print(" BP ");
        Serial.print(both_pressed);
        if (rol_pos[(i/2)] == 8) {
          rol_pos[(i/2)] = 16;
          keys &= ~(1 << (i));
          both_pressed &= ~(1 << (i));
          half_time = true;
          Serial.print(" H16 ");
        }
        else if (rol_pos[(i/2)] == 2) {
          rol_pos[(i/2)] = 64;
          keys &= ~(1 << (i+1));
          both_pressed &= ~(1 << (i));
          half_time= true;
          Serial.print(" H64 ");
        }
 /*       else {  //nach oben, lassen aber both_pressed an....
//          rol_pos[(i/2)] = 1;
          keys &= ~(1 << (i));
          Serial.print(" HOben ");
        }
*/
//        if (( i % 2) == 0) rol_pos[(i/2)] = 16;
//        else               rol_pos[(i/2)] = 64;
//        time_tab[i] = zeit.sec + time_tab_half[i]; // setze neue Endzeit f¸r Taster
//        rol_pos[(i/2)] |= rol_pos_old;

    }   
    if (((keys & (1 << i)) == 0) && ((old_keys & (1 << i)) > 0)) { // Taste wurde gedr¸ckt "fallende Flanke"
      if (half_time) {
        if (( i % 2) == 0) rol_pos[(i/2)] = 16;
        else               rol_pos[(i/2)] = 64;
        time_tab[i] = zeit.sec + time_tab_half[i]; // setze neue Endzeit f¸r Taster
        rol_pos[(i/2)] |= rol_pos_old;
      }
      else 
      
      {
        if (( i % 2) == 0) rol_pos[(i/2)] = 1;
        else               rol_pos[(i/2)] = 4;
        time_tab[i] = zeit.sec + time_tab_total[i]; // setze neue Endzeit f¸r Taster
      }
      if (time_tab[i] > 59) time_tab[i] = time_tab[i] - 59; // Berechne und Korrigiere ‹berlauf
      if ((old_value & (1 << i)) == 0) {
        Serial.print("old0 ");
        rol_pos[(i/2)] = 128;
        new_value |= (1 << i); // setze virtuellen Taster xxx
        if ((i % 2) == 0) {
            new_value |= (1 << (i+1));
            key_active = true;
            Serial.print("mod02 ");
        }
        else if (i > 0) {
          rol_pos[(i/2)] = 128;
          new_value |= (1 << (i-1)); // setze virtuellen Taster xxx      
          key_active = true;
          Serial.print("!mod02 "); 
        }
      }
      if ((i % 2) == 0) {
        if ((old_value & (1 << (i+1))) == 0) {
          rol_pos[(i/2)] = 128;
          Serial.print("mod03 ");
           new_value |= (1 << (i+1));
           key_active = true;
        }
      }
      else if ((i > 0) && ((old_value & (1 << (i-1))) == 0)) {
          rol_pos[(i/2)] = 128;
          Serial.print("!mod03 ");
          new_value |= (1 << (i-1)); // setze virtuellen Taster xxx     
          key_active = true;  
        }
      
      if (key_active == false) {
        Serial.print("mod04 \n");
        new_value &= ~(1 << i); // lˆsche (aktiviere) virtuellen Tater 
      }
    }
 
    key_active = false;
  }
  return (new_value);
}

uint16_t continue_time(uint16_t old_value) // Pr¸ft auf Abschalt(zeit)bedingung pro Taster
{
  uint16_t i, new_value;
  uint8_t rol_pos_old;
  
  // (1<<i) kann nch vereinfacht werden, wenn in jedem Schleifendurchlauf um 1 geschoben wird ...
  new_value = old_value;
  for (i = 0; i < 16; i++) {
    rol_pos_old = rol_pos[(i/2)];
    if  ((old_value & (1 << i)) == 0) { // ist v-taster gedr¸ckt ?
      if (time_tab[i] == zeit.sec) {
        new_value |= (1 << i); // pr¸fe ob zeit in tab erreicht
        Serial.print("-");
        if (rol_pos[(i/2)] == 64) rol_pos[(i/2)] = 32;
        else rol_pos[(i/2)] = (rol_pos[(i/2)]<<1); // shift 1 bit left to mark endposition
        if ((rol_pos[(i/2)] == 32) && (rol_pos_old >= 128)) rol_pos[(i/2)] = 128;
      }
    }
  }
  return (new_value);
}

uint8_t is_printable(char in)
{
  if ((in > 32) && (in < 127)) return (1);
  else return (0);
}



void inp_string(char text[], int8_t maxchar  ) // Zeiger auf String und maximle Anzahl von Bytes (incl. \0)
{
  uint8_t i;
  uint8_t t;

  i = 0;
  t = '\0';
  maxchar--; // maxchar Anzahl inclusive \0 daher 1 abziehen u, platz f¸r \0 zu resevieren
  while (t != '\n') {         // cr als Eingabeende
    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = atoi(Serial.read());
    Serial.print(t);
    if ((maxchar > i + 1) && is_printable(t)) { // Maximale L‰nge n0ch nicht erreicht ?
      text[i] = (t >> 4);
      i++;
      text[i] = (t &= 0x0f);

      Serial.print(t);// Echo der Eingabe nur wenn nicht cr
      text[i] = '\0'; //n‰chstes Zeichen wird \0
    }

  }
}

void inp_bits(uint16_t *zahl) // Zeiger auf long
{
  uint8_t i, maxbits;
  int t;

  t = '\0';
  maxbits = 16; // Rolladensteuerung nutzt 12 BIT
  i = maxbits; // i=1->BIT0, i=2->BIT1 ...

  if (Serial.available() > 0) {
    // read the incoming byte:
    t = Serial.read();
  }
  while (i >= 1) {         // cr als Eingabeende
    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = Serial.read();
    if (i > 0) { // Maximale L‰nge n0ch nicht erreicht ?
      if (t == '1') {
        *zahl = *zahl | (1 << (i - 1));
        Serial.print('1');
        i--;
      } else {
        *zahl = *zahl & ~(1 << (i - 1));
        Serial.print('0');
        i--;
      }
    }
  }
}


void inp_time(volatile datestruct *zeit) // Zeiger auf zeit-sruct
{
  uint8_t int_inp;
  //Stunde
  Serial.print("Stunde: (");
  strcpy(string15, "");
  itos(string15, zeit->hour, 2);
  Serial.print(string15); // string15 mit akt. Wert laden
  Serial.print("): ");
  while (Serial.available() < 1) {
    ;  //wartet auf Eingabe
  }
  int_inp = Serial.parseInt();
  zeit->hour = int_inp;
  Serial.print(int_inp);
  //Minute
  Serial.print("\nMinute: (");
  strcpy(string15, "");
  itos(string15, zeit->min, 2);
  Serial.print(string15); // string15 mit akt. Wert laden
  Serial.print("): ");
  while (Serial.available() < 1) {
    ;  //wartet auf Eingabe
  }
  int_inp = Serial.parseInt();
  zeit->min = int_inp;
  Serial.print(int_inp);
  //Sekunde
  Serial.print("\nSekunde: (");
  strcpy(string15, "");
  itos(string15, zeit->sec, 2);
  Serial.print(string15); // string15 mit akt. Wert laden
  Serial.print("): ");
  while (Serial.available() < 1) {
    ;  //wartet auf Eingabe
  }
  while (Serial.available() < 1) {
    ;  //wartet auf Eingabe
  }
  int_inp = Serial.parseInt();
  zeit->sec = int_inp;
  Serial.print(int_inp);
}

void anzeigen( void ) // ruft konfig-menue auf
{
  char *wday_text[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  uint8_t i;

  //Uptime
  Serial.print("\n\nUptime: ");
  strcpy(string60, "");
  ltos(string15, uptime.day, 5);
  strcat(string60, string15);
  strcat(string60, "d ");
  itos(string15, uptime.hour, 2);
  strcat(string60, string15);
  strcat(string60, "h ");
  itos(string15, uptime.min, 2);
  strcat(string60, string15);
  strcat(string60, "m ");
  itos(string15, uptime.sec, 2);
  strcat(string60, string15);
  strcat(string60, "s ");
  Serial.print(string60);
  //Datum
  Serial.print("\n");
  Serial.print("Datum+Uhrzeit: ");
  strcpy(string60, "");
  strcat(string60, wday_text[zeit.wday]);
  strcat(string60, " ");
  itos(string15, zeit.day, 2);
  strcat(string60, string15);
  strcat(string60, ".");
  itos(string15, zeit.month, 2);
  strcat(string60, string15);
  strcat(string60, ".");
  ltos(string15, zeit.year, 4);
  strcat(string60, string15);
  Serial.print(string60);
  //Uhrzeit
  // uart_putcrlf();
  Serial.print(" ");
  out_time(zeit);
  Serial.print(string60);
  //Quarzkorektur
  Serial.print("\nQuarz-Korrektur: ");
  ltos(string60, quarz_adjustment, 4);
  Serial.print(string60);
  //Uhrzeit Mo-Fr
  Serial.print("\nUhrzeit Mo-Fr hoch:   ");
  out_time(hoch_mofr);
  Serial.print(string60);
  //Uhrzeit
  //uart_putcrlf();
  Serial.print(" runter:");
  strcpy(string60, "");
  out_time(runter_mofr);
  Serial.print(string60);
  //Uhrzeit
  Serial.print("\nUhrzeit Sa+So hoch: ");
  out_time(hoch_saso);
  Serial.print(string60);
  //Uhrzeit0
  Serial.print(" runter:");
  out_time(runter_saso);
  Serial.print(string60);
  //Zeit-enable
  Serial.print("\nZeitsteuerung:        ");
  itos(string60, zeitsteuer_enable, 1);
  Serial.print(string60);
  //zeitgesteuert-Tasten
  Serial.print("\nZeitgest.-Tasten hoch: ");
  strcpy(string60, "");
  print_nibble(zeitgest_hoch_val, 3);
  print_nibble(zeitgest_hoch_val, 2);
  print_nibble(zeitgest_hoch_val, 1);
  print_nibble(zeitgest_hoch_val, 0);
  Serial.print(string60);
  Serial.print(" runter: ");
  strcpy(string60, "");
  print_nibble(zeitgest_runter_val, 3);
  print_nibble(zeitgest_runter_val, 2);
  print_nibble(zeitgest_runter_val, 1);
  print_nibble(zeitgest_runter_val, 0);
  Serial.print(string60);
  //Haltezeiten
  Serial.print("\nHaltezeiten:\n");
  strcpy(string60, "");
  for (i = 0; i < 16; i++) {
    itos(string15, i, 2);
    strcat(string60, string15);
    strcat(string60, " ");
  }
  Serial.print(string60);
  Serial.print("\n");
  strcpy(string60, "");
  for (i = 0; i < 16; i++) {
    itos(string15, time_tab_total[i], 2);
    strcat(string60, string15);
    strcat(string60, " ");
  }
  Serial.print(string60);
  Serial.print("\n");
  strcpy(string60, "");
  for (i = 0; i < 16; i++) {
    itos(string15, time_tab_half[i], 2);
    strcat(string60, string15);
    strcat(string60, " ");
  }
  Serial.print(string60);
  //Doppelklick
  Serial.print("\nDoppelklick: ");
  ltos(string60, doppel_klick_val, 4);
  Serial.print(string60);
  //template_nr
  Serial.print("\nDoppelklick-Template: ");
  itos(string60, template_nr, 1);
  Serial.print(string60);
  //Mapping
  Serial.print("\nDoppelklick-Mapping:\n");
  for (i = 0; i < 16; i += 2) {
    Serial.print("Taste #");
    itos(string60, i, 2);
    Serial.print(string60);
    Serial.print(" (hoch): ");
    strcpy(string60, "");
    print_nibble(template_table[template_nr][i], 3);
    print_nibble(template_table[template_nr][i], 2);
    print_nibble(template_table[template_nr][i], 1);
    print_nibble(template_table[template_nr][i], 0);
    Serial.print(string60);
    Serial.print(" #");
    itos(string60, i + 1, 2);
    Serial.print(string60);
    Serial.print(" (runter): ");
    strcpy(string60, "");
    print_nibble(template_table[template_nr][i + 1], 3);
    print_nibble(template_table[template_nr][i + 1], 2);
    print_nibble(template_table[template_nr][i + 1], 1);
    print_nibble(template_table[template_nr][i + 1], 0);
    Serial.print(string60);
    Serial.print("\nBitte Taste drücken\n");
  }
  //auf Tastendruck warten
  while (Serial.available() < 1) {
    ;    //wartet auf Eingabe
  }
  i = Serial.read();
}

void fernsteuerung_rolladen( void ) // manuelle Fernbedieunung ¸ber das Men¸
{
  uint8_t t;
  uint8_t exit = 0;

  while (exit == 0) {

    Serial.print("\nFernsteuerung:\n");
    Serial.print("A - 0 hoch   (Balkon)\n");
    Serial.print("B - 0 runter (Balkon)\n");
    Serial.print("C - 1 hoch   (Buero Stein)\n");
    Serial.print("D - 1 runter (Buero Stein)\n");
    Serial.print("E - 2 hoch   (Buero Strasse)\n");
    Serial.print("F - 2 runter (Buero Strasse\n");
    Serial.print("G - 3 hoch   (Wohnzimmer)\n");
    Serial.print("H - 3 runter (Wohnzimmer)\n");
    Serial.print("I - 4 hoch   (Esszimmer)\n");
    Serial.print("J - 4 runter (Esszimmer)\n");
    Serial.print("K - 5 hoch   (Terasse)\n");
    Serial.print("L - 5 runter (Terasse)\n");
    Serial.print("M - 6 hoch   (Kueche)\n");
    Serial.print("N - 6 runter (Kueche)\n");
    Serial.print("O - 7 hoch   (unused)\n");
    Serial.print("P - 7 runter (unused)\n");
    Serial.print("Y - Alle hoch\n");
    Serial.print("Z - Alle runter\n");
    Serial.print("X - Menu Verlassen\n");
    Serial.print("Kommando eingeben:");

    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = Serial.read();


    // Matrix:
    // 65535 = 1111111111111111 alle hoch
    // 0xAAAA =  1010010101010101 alle runter
    //
    //
    //
    //


    uint16_t rolladen_bits;


    switch (t) {
      case 'a'  :
        rolladen_bits = map_rolladen_bits(0, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'b'  :
        rolladen_bits = map_rolladen_bits(0, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'c'  :
        rolladen_bits = map_rolladen_bits(1, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'd'  :
        rolladen_bits = map_rolladen_bits(1, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'e'  :
        rolladen_bits = map_rolladen_bits(2, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'f'  :
        rolladen_bits = map_rolladen_bits(2, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'g'  :
        rolladen_bits = map_rolladen_bits(3, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'h'  :
        rolladen_bits = map_rolladen_bits(3, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'i'  :
        rolladen_bits = map_rolladen_bits(4, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'j'  :
        rolladen_bits = map_rolladen_bits(4, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'k'  :
        rolladen_bits = map_rolladen_bits(5, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'l'  :
        rolladen_bits = map_rolladen_bits(5, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'm'  :
        rolladen_bits = map_rolladen_bits(6, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'n'  :
        rolladen_bits = map_rolladen_bits(6, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'o'  :
        rolladen_bits = map_rolladen_bits(7, 0); //0=hoch
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'p'  :
        rolladen_bits = map_rolladen_bits(7, 1); //1=runter
        switch_relais(rolladen_bits, output_ist);
        output_ist = rolladen_bits;
        //exit=1;
        break;
      case 'y'  :
        switch_relais(0x55555, output_ist);
        output_ist = 0x9999; //faehrt alle rolladen hoch
        //exit=1;
        break;

      case 'z'  :
        switch_relais(0xFFFF, output_ist);
        output_ist = 0xFFFF; //faehrt alle rolladen runter
        //exit=1;
        break;


      case 'x'  :
        Serial.print("\n");
        exit = 1;
        break;
    }

  }
}

void konfig_zeit( void ) // ruft zeit-menue auf
{
  uint16_t t, int_inp;
  uint8_t exit = 0;
  uint16_t y;

  while (exit == 0) {
    Serial.print("\nZeitinformationen etc. eingeben:\n");
    Serial.print("U - Uhrzeit\n");
    Serial.print("D - Datum\n");
    Serial.print("W - Schaltzeiten Mo.-Fr.\n");
    Serial.print("S - Schaltzeiten Sa.+So.\n");
    Serial.print("E - Zeitsteuerung ein/aus\n");
    Serial.print("M - Mapping fuer Zeitsteuerung\n");
    Serial.print("Q - Quarz-Korrektur\n");
    Serial.print("A - Anzeige der Konfiguration\n");
    Serial.print("X - Menu Verlassen\n");
    Serial.print("Kommando eingeben:\n");

    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = Serial.read();

    switch (t) {
      case 'u'  :
        Serial.print("Aktuelle Uhrzeit eingeben:\n");
        inp_time(&zeit);
        break;

      case 'w'  :
        Serial.print("Uhrzeit Rolladen oeffnen Mo.-Fr. eingeben:\n");
        inp_time(&hoch_mofr);

        Serial.print("Uhrzeit Rolladen schliessen Mo.-Fr. eingeben:\n");
        inp_time(&runter_mofr);
        break;

      case 's'  :
        Serial.print("Uhrzeit Rolladen oeffnen Sa.+So. eingeben:\n");
        inp_time(&hoch_saso);

        Serial.print("Uhrzeit Rolladen schliessen Sa.+So. eingeben:\n");
        inp_time(&runter_saso);
        break;

      case 'd'  :
        Serial.print("aktuelles Datum eingeben:\n");
        // Tag
        Serial.print("\nTag:");
        Serial.print("(");
        strcpy(string15, "");
        itos(string15, zeit.day, 2);
        Serial.print(string15); // string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        int_inp = Serial.parseInt();
        zeit.day = int_inp;
        Serial.print(int_inp);
        //Monat
        Serial.print("\nMonat:");
        Serial.print("(");
        strcpy(string15, "");
        itos(string15, zeit.month, 2);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        int_inp = Serial.parseInt();
        zeit.month = int_inp;
        Serial.print(int_inp);
        //Jahr
        Serial.print("\nJahr:");
        ("(");
        strcpy(string15, "");
        ltos(string15, zeit.year, 4);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        y = Serial.parseInt();
        zeit.year = y;
        Serial.print(y);
        //Wochentag
        Serial.print("\nWochentag (So=0, Mo=1, Di=2, Mi=3, Do=4, Fr=5, Sa=6):");
        Serial.print("(");
        strcpy(string15, "");
        itos(string15, zeit.wday, 1);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        int_inp = Serial.parseInt();
        zeit.wday = int_inp;
        Serial.print(int_inp);
        break;

      case 'q'  :
        Serial.print("\nQuarz-Korrektur:");
        Serial.print("(");
        strcpy(string15, "");
        ltos(string15, quarz_adjustment, 4);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        int_inp = Serial.parseInt();
        quarz_adjustment = int_inp;
        Serial.print(int_inp);
        break;


      case 'm'  :
        Serial.print("\nTastencode hoch:");
        Serial.print("(");
        print_nibble(zeitgest_hoch_val, 3);
        print_nibble(zeitgest_hoch_val, 2);
        print_nibble(zeitgest_hoch_val, 1);
        print_nibble(zeitgest_hoch_val, 0);
        Serial.print("):");
        inp_bits(&zeitgest_hoch_val);

        Serial.print("\nTastencode runter:");
        Serial.print("(");
        print_nibble(zeitgest_runter_val, 3);
        print_nibble(zeitgest_runter_val, 2);
        print_nibble(zeitgest_runter_val, 1);
        print_nibble(zeitgest_runter_val, 0);
        Serial.print("):");
        inp_bits(&zeitgest_runter_val);
        break;

      case 'e'  :
        Serial.print("\nZeitsteuerung 0=disable, 1=enable:");
        Serial.print("(");
        strcpy(string15, "");
        itos(string15, zeitsteuer_enable, 1);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        int_inp = Serial.parseInt();
        zeitsteuer_enable = int_inp;
        Serial.print(int_inp);
        Serial.print("\n");
        break;

      case 'x'  :
        Serial.print("\n");
        exit = 1;
        break;
      case 'a'  :
        anzeigen();
        break;
    }
    Serial.print("\n");
  }
}

uint16_t map_rolladen_bits( uint8_t rolladennummer, uint8_t richtung )
{
  //bit0: Schalten, bit1: Richtung, ..usw
  //   PORTL   PORTK
  // 11111100 00000000
  // 54321098 76543210
  // RSRSRSRS RSRSRSRS
  // 77665544 33221100
  uint16_t rolladen_bit_tab[2][8] = { { 0x0001, 0x0004, 0x0010, 0x0040, 0x0100, 0x0400, 0x1000, 0x4000 }, //hoch
    { 0x0003, 0x000c, 0x0030, 0x00c0, 0x0300, 0x0c00, 0x3000, 0xc000 }
  }; //runter

  return (rolladen_bit_tab[richtung][rolladennummer]);
}



void wizzard( void ) // Ermittelt Rolladen-laufzeiten runter und hoch f¸r vorgegebenen Rolladen
{
  uint8_t rolladen_nr, wizzard_sec_stop, wizzard_sec_start, wizzard_sec_half, wizzard_dt;
  uint16_t rolladen_bits;

  //Rolladen ausw‰hlen
  Serial.print("\nRolladen auswaehlen (0-7):");
  while (Serial.available() < 1) {
    ;  //wartet auf Eingabe
  }
  rolladen_nr = Serial.parseInt();


  //gew‰hlter Rolladen hoch fahren
  rolladen_bits = map_rolladen_bits(rolladen_nr, 0); //0=hoch
  switch_relais(rolladen_bits, output_ist);
  output_ist = rolladen_bits;

  //Auf Tastendruck warten
  Serial.print("\nRolladentaste druecken wenn Rolladen hochgefahren ist\n");
  real_keys_pressed = 0; // abfrage initialisieren
  while (!real_keys_pressed) {
    ;    //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)
  }

  //Timer Starten+Rolladen nach unten fahren
  rolladen_bits = map_rolladen_bits(rolladen_nr, 1); //1=runter
  wizzard_sec_start = zeit.sec;
  switch_relais(rolladen_bits, output_ist);
  output_ist = rolladen_bits;

  //auf tastendruck warten
  Serial.print("Rolladentaste druecken wenn Rolladen halb runtergefahren ist\n");
  real_keys_pressed = 0; // abfrage initialisieren
  while (!real_keys_pressed) {
    ;    //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)
  }

  //Timer+Rolladen stoppen
  wizzard_sec_half = zeit.sec;

  //dt ermitteln und abspeichern
  if (wizzard_sec_half < wizzard_sec_start) wizzard_sec_half += 60;
  wizzard_dt = wizzard_sec_half - wizzard_sec_start;
  time_tab_half[rolladen_nr * 2] = wizzard_dt;

  //auf tastendruck warten
  Serial.print("Rolladentaste druecken wenn Rolladen runtergefahren ist\n");
  real_keys_pressed = 0; // abfrage initialisieren
  while (!real_keys_pressed) {
    ;    //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)
  }

  //Timer+Rolladen stoppen
  switch_relais(0, output_ist);
  output_ist = 0;
  wizzard_sec_stop = zeit.sec;

  //dt ermitteln und abspeichern
  if (wizzard_sec_stop < wizzard_sec_start) wizzard_sec_stop += 60;
  wizzard_dt = wizzard_sec_stop - wizzard_sec_start;
  time_tab_total[rolladen_nr * 2] = wizzard_dt + 1; //sicherheitshalber eine Sekunde zugeben !

  //auf tastendruck warten
  //real_keys_pressed=0; // abfrage initialisieren
  //while (!real_keys_pressed) { ;} //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)

  //Timer Starten+Rolladen nach oben fahren
  rolladen_bits = map_rolladen_bits(rolladen_nr, 0); //0=hoch
  wizzard_sec_start = zeit.sec;
  switch_relais(rolladen_bits, output_ist);
  output_ist = rolladen_bits;

  //auf tastendruck warten
  Serial.print("Rolladentaste druecken wenn Rolladen halb hochgefahren ist\n");
  real_keys_pressed = 0; // abfrage initialisieren
  while (!real_keys_pressed) {
    ;    //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)
  }

  wizzard_sec_half = zeit.sec;

  //dt ermitteln und abspeichern
  if (wizzard_sec_half < wizzard_sec_start) wizzard_sec_half += 60;
  wizzard_dt = wizzard_sec_half - wizzard_sec_start;
  time_tab_half[rolladen_nr * 2 + 1] = wizzard_dt + 1;



  //auf tastendruck warten
  Serial.print("Rolladentaste druecken wenn Rolladen hochgefahren ist\n");
  real_keys_pressed = 0; // abfrage initialisieren
  while (!real_keys_pressed) {
    ;    //wartet auf Eingabe (Achtung: irgendeine Rolladentaste !!!!)
  }
  //Timer stoppen
  switch_relais(0, output_ist);
  output_ist = 0;
  wizzard_sec_stop = zeit.sec;

  //dt ermitteln und abspeichern
  if (wizzard_sec_stop < wizzard_sec_start) wizzard_sec_stop += 60;
  wizzard_dt = wizzard_sec_stop - wizzard_sec_start;
  time_tab_total[rolladen_nr * 2 + 1] = wizzard_dt + 1;

  //Tastendruck verwerfen
  real_keys = 65535;
  real_keys_old = real_keys;
}

void konfig_rolladen( void ) // ruft rolladen-menue auf
{
  uint8_t t;
  uint8_t i;
  uint8_t exit = 0;

  while (exit == 0) {
    t = '#';
    Serial.print("\nRolladen-Parameter:\n");
    Serial.print("Z - Laufzeiten der Rolladen eingeben\n");
    Serial.print("W - Wizzard: Laufzeiten der Rolladen ermitteln\n");
    Serial.print("S - Doppelklick-Geschwindigkeit\n");
    Serial.print("T - Auswahl Doppelklick-Template\n");
    Serial.print("M - Editieren Doppelklick-Template #3\n");
    Serial.print("H - Alle Rolladen hoch\n");
    Serial.print("R - Alle Rolladen runter\n");
    Serial.print("A - Anzeige der Konfiguration\n");
    Serial.print("X - Menu verlassen\n");
    Serial.print("Kommando eingeben:");

    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = Serial.read();

    switch (t) {

      case 's'  :
        Serial.print("\nDoppelklick-Geschwindigkeit(1-1000):(");
        strcpy(string15, "");
        ltos(string15, doppel_klick_val, 4);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        doppel_klick_val = Serial.parseInt();
        Serial.print(doppel_klick_val);
        break;

      case 'h'  :
        switch_relais(0x5555, output_ist);
        output_ist = 0x5555; //faehrt alle rolladen hoch
        Serial.print("\nTaste druecken\n");
        while (Serial.available() < 1) {
          ;    //wartet auf Eingabe
        }
        i = Serial.read();
        break;

      case 'r'  :
        switch_relais(0xffff, output_ist);
        output_ist = 0xffff; // faert alle rolladen unter
        Serial.print("\nTaste druecken\n");
        while (Serial.available() < 1) {
          ;    //wartet auf Eingabe
        }
        i = Serial.read();
        break;

      case 'z'  :
        for (i = 0; i < 16; i++) {
          Serial.print("\nHaltezeit Taster ");
          strcpy(string60, "");
          itos(string60, i, 2);
          Serial.print(string60);
          Serial.print(" eingeben (");
          strcpy(string15, "");
          itos(string15, time_tab_total[i], 2);
          Serial.print(string15);// string15 mit akt. Wert laden
          Serial.print("):");
          while (Serial.available() < 1) {
            ;  //wartet auf Eingabe
          }
          time_tab_total[i] = Serial.parseInt();
          Serial.print(time_tab_total[i]);
        }
        Serial.print("\n");
        break;

      case 'w'  :
        wizzard();
        break;

      case 't'  :
        Serial.print("\nTemplate-Auswahl (0-3): (");
        strcpy(string15, "");
        itos(string15, template_nr, 3);
        Serial.print(string15);// string15 mit akt. Wert laden
        Serial.print("):");
        while (Serial.available() < 1) {
          ;  //wartet auf Eingabe
        }
        template_nr = Serial.parseInt();
        Serial.print(template_nr);
        Serial.print("\n");
        break;

      case 'm'  :
        Serial.print("\nDoppelklick_Template-Auswahl\n");
        for (i = 0; i < 16; i++) {
          Serial.print("Tastencode fuer Taster #");
          strcpy(string60, "");
          itos(string60, i, 2);
          Serial.print(string60);
          Serial.print(" eingeben (");
          print_nibble(template_table[3][i], 3);
          print_nibble(template_table[3][i], 2);
          print_nibble(template_table[3][i], 1);
          print_nibble(template_table[3][i], 0);
          Serial.print("):");
          inp_bits(&template_table[3][i]);
          Serial.print("\n");
        }
        break;

      case 'x'  :
        exit = 1;
        break;

      case 'a'  :
        anzeigen();
        break;
    }
    Serial.print("XEND\n");
  }
}

void load_eeprom_konfig( void )
{
  ee_addr = 0;
  EEPROM.get(ee_addr, quarz_adjustment);
  ee_addr += sizeof(uint16_t);
  EEPROM.get(ee_addr, template_nr);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, doppel_klick_val);
  ee_addr += sizeof(uint16_t);
  EEPROM.get(ee_addr, trippel_klick_val);
  ee_addr += sizeof(uint16_t);
  //Zeitsteuerung
  EEPROM.get(ee_addr, hoch_mofr.sec);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, hoch_mofr.min);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, hoch_mofr.hour);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_mofr.sec);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_mofr.min);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_mofr.hour);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, hoch_saso.sec);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, hoch_saso.min);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, hoch_saso.hour);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_saso.sec);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_saso.min);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, runter_saso.hour);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, zeitsteuer_enable);
  ee_addr += sizeof(uint8_t);
  EEPROM.get(ee_addr, zeitgest_hoch_val);
  ee_addr += sizeof(uint16_t);
  EEPROM.get(ee_addr, zeitgest_runter_val);
  ee_addr += sizeof(uint16_t);
  for (int8_t count = 0; count < 16; count++) {
    EEPROM.get(ee_addr, time_tab_total[count]);
    ee_addr += sizeof(uint16_t);
    EEPROM.get(ee_addr, time_tab_half[count]);
    ee_addr += sizeof(uint16_t);
    EEPROM.get(ee_addr, template_table[3][count]);
    ee_addr += sizeof(uint16_t);
  }
}


boolean compare( const volatile datestruct & zeit1, const volatile datestruct & zeit2 )     // vergleicht die aktuelle Zeit mit den Schaltzeiten
{
  uint8_t hcmp = 0;
  uint8_t mcmp = 0;
  uint8_t scmp = 0;
  boolean ret = 0;

  if (zeit1.hour == zeit2.hour) hcmp = 1;
  if (zeit1.min == zeit2.min) mcmp = 1;
  if (zeit1.sec == zeit2.sec) scmp = 1;
  if ((hcmp == 1) && (mcmp == 1) && (scmp == 1)) {
    ret = 1; // 1 wenn Zeiten gleich sind
  } else {
    ret = 0;
  }
  return (ret);
}

void konfig( void ) // ruft konfig-menue auf
{
  char t;
  uint8_t exit = 0;
  uint8_t var8 = 0;
  uint16_t var16 = 0;
  uint16_t rolladen_bits = 65535;
//  uint16_t ee_addr = 0;
  uint8_t ee_val;
  
  while (exit == 0) {
    t = '#';
    Serial.print("\nHauptmenue:\n");
    Serial.print("Z - Einstellung Zeit, Datum etc.\n");
    Serial.print("F - Fernsteuerung hoch/runter.\n");
    Serial.print("R - Einstellung der Rollaeden\n");
    Serial.print("A - Anzeigen der aktuellen Einstellungen\n");
    Serial.print("S - Konfiguration-Speichern\n");
    Serial.print("L - Konfiguration-Laden\n");
    Serial.print("X - Menu verlassen\n");
    Serial.print("Kommando eingeben:");

    while (Serial.available() < 1) {
      ;    //wartet auf Eingabe
    }
    t = Serial.read();
    //uart_putc(t);

    switch (t) {
      case 'z'  :
        konfig_zeit();
        break;

      case 'r'  :
        konfig_rolladen();
        break;

      case 'f'  :
        fernsteuerung_rolladen();
        break;


      case 'a'  :
        anzeigen();
        break;

      case 's'  : // Sichert ins EEPROM:
        ee_addr = 0;
        EEPROM.put(ee_addr, quarz_adjustment);
        ee_addr += sizeof(uint16_t);
        EEPROM.put(ee_addr, template_nr);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, doppel_klick_val);
        ee_addr += sizeof(uint16_t);
        EEPROM.put(ee_addr, trippel_klick_val);
        ee_addr += sizeof(uint16_t);
//        Serial.print("X\n");
        //Zeitsteuerung
        EEPROM.put(ee_addr, hoch_mofr.sec);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, hoch_mofr.min);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, hoch_mofr.hour);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_mofr.sec);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_mofr.min);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_mofr.hour);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, hoch_saso.sec);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, hoch_saso.min);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, hoch_saso.hour);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_saso.sec);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_saso.min);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, runter_saso.hour);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, zeitsteuer_enable);
        ee_addr += sizeof(uint8_t);
        EEPROM.put(ee_addr, zeitgest_hoch_val);
        ee_addr += sizeof(uint16_t);
        EEPROM.put(ee_addr, zeitgest_runter_val);
        ee_addr += sizeof(uint16_t);
 //       Serial.print("Xfor\n");
        for (int8_t count = 0; count < 16; count++) {
 //         Serial.print("X");
 //         Serial.print(count);
          EEPROM.put(ee_addr, time_tab_total[count]);
          ee_addr += sizeof(uint16_t);
          EEPROM.put(ee_addr, time_tab_half[count]);
          ee_addr += sizeof(uint16_t);
          EEPROM.put(ee_addr, template_table[3][count]);
          ee_addr += sizeof(uint16_t);
        }
        break;

      case 'l'  : // L‰dt Konfig aus EEPROM
        load_eeprom_konfig();
        break;

      case 'e' :
        ee_addr = 0;
        Serial.print("\n");
        while (ee_addr < EEPROM.length()) {
          ee_val = EEPROM.read(ee_addr);
          Serial.print(ee_addr);
          Serial.print(" : ");
          Serial.print(ee_val);
          Serial.print("\n");
          ee_addr++;
        }
        break;
      
      case 'x'  :
        exit = 1;
        break;
    }
    Serial.print("X\n");
  }
}



void loop() {
  char t;
  boolean remote = false;
  boolean half_way = false;
  uint16_t remote_key = 65535;
  
  if (Serial.available() > 0) {
    // read the incoming byte:
    t = Serial.read();

    switch (t) {
      case 'a'  :
        remote_key = 0xFFFE;
        remote = true;
        break;
      case 'A' :
        both_pressed = ~0xFFFE;
        //half_way = true;
        break;
      case 'b'  :
        remote_key = 0xFFFD;
        remote = true;
        break;
      case 'B' :
        both_pressed = ~0xFFFE;
        remote = true;
        break;
      case 'c'  :
        remote_key = 0xFFFB;
        remote = true;
        break;
      case 'C' :
        both_pressed = ~0xFFFB;
        remote = true;
        break;
      case 'd'  :
        remote_key = 0xFFF7;
        remote = true;
        break;
      case 'D' :
        both_pressed = ~0xFFFB;
        remote = true;
        break;
      case 'e'  :
        remote_key = 0xFFEF;
        remote = true;
        break;
      case 'E' :
        both_pressed = 0xFFEF;
        remote = true;
        break;
      case 'f'  :
        remote_key = 0xFFDF;
        remote = true;
        break;
      case 'F' :
        both_pressed = ~0xFFEF;
        remote = true;
        break;
      case 'g'  :
        remote_key = 0xFFBF;
        remote = true;
        break;
      case 'G' :
        both_pressed = ~0xFFBF;
        remote = true;
        break;
      case 'h'  :
        remote_key = 0xFF7F;
        remote = true;
        break;
      case 'H' :
        both_pressed = ~0xFFBF;
        remote = true;
        break;
      case 'i'  :
        remote_key = 0xFEFF;
        remote = true;
        break;
      case 'I' :
        both_pressed = ~0xFEFF;
        remote = true;
        break;
      case 'j'  :
        remote_key = 0xFDFF;
        remote = true;
        break;
      case 'J' :
        both_pressed = ~0xFEFF;
        remote = true;
        break;
      case 'k'  :
        remote_key = 0xFBFF;
        remote = true;
        break;
      case 'K' :
        both_pressed = ~0xFBFF;
        remote = true;
        break;
      case 'l'  :
        remote_key = 0xF7FF;
        remote = true;
        break;
      case 'L' :
        both_pressed = ~0xFBFF;
        remote = true;
        break;
      case 'm'  :
        remote_key = 0xEFFF;
        remote = true;
        break;
      case 'M' :
        both_pressed = ~0xEFFF;
        half_way = true;
        break;
      case 'n'  :
        remote_key = 0xDFFF;
        remote = true;
        break;
      case 'N' :
        both_pressed = ~0xEFFF;
        remote = true;
        break;
      case 'y'  :
        remote_key = 0xEAAA;
        remote = true;
        break;
      case 'Y' :
        both_pressed = ~0xEAAA;
        remote = true;
        break;
      case 'z'  :
        remote_key = 0xD555;
        remote = true;
        break;
      case 'Z' :
        both_pressed = ~0xEAAA;
        remote = true;
        break;
      case '@' :
        konfig();
        break;  
    }
  }
  //Erzeugung 12-BIT-Zahl aus Eingangstasten wird in Interrupt-Routine erzeugt
  // real_keys enth‰lt Taste, Doppel enth‰lt die Doppelklicks

  v_keys = real_keys;
  if (doppel_klick != 0) {
  Serial.print(" doppel_klick5 ");
  Serial.print(doppel_klick);
    v_keys = map_keys(~doppel_klick); //doppel- ¸berschreibt einzel-klick
 //   v_keys |= doppel_klick; // Blendet urspr¸nglich gedr¸ckte Taste aus (sonst Stopp-Funktionf¸r diese Tase)
  }

  //zeitgesteuert
  if (zeitgesteuert != 0) {
    v_keys = zeitgesteuert;
    zeitgesteuert = 0;
  }

  // direct remote controll
  if (remote) {
    v_keys = remote_key;
  }
  //Laufzeit pro Taster Setzen
  v_keys_timer = startstop_time(v_keys, v_keys_old, v_keys_timer);
  //    Serial.print(" | ");

  if ((zeit.sec != old_sec) || (v_keys != v_keys_old)) {
    if (zeit.sec != old_sec) {
      v_keys_timer = continue_time(v_keys_timer);
      //Serial.print(" | ");

      //Schaltzeiten f¸r zeitgesteuerten-Mode
      if (zeitsteuer_enable != 0) {
        if ((zeit.wday > 0) && (zeit.wday < 6)) { //Mo-Fr
          if ( compare(zeit, hoch_mofr ) == 1 ) {
            zeitgesteuert = zeitgest_hoch_val;  // 101010101010 -> hier kann Rolladenmapping gemacht werden
          }
          if (compare(zeit, runter_mofr) == 1) zeitgesteuert = zeitgest_runter_val; // 010101010101 -> hier kann Rolladenmapping gemacht werden
        }
        else { // Sa+So
          if (compare(zeit, hoch_saso) == 1) zeitgesteuert = zeitgest_hoch_val; // 101010101010 -> hier kann Rolladenmapping gemacht werden
          if (compare(zeit, runter_saso) == 1) zeitgesteuert = zeitgest_runter_val; // 010101010101 -> hier kann Rolladenmapping gemacht werden
        }
      }
    }

    //gibt uhrzeit aus
    Serial.print(" Z:");
    //      out_time(zeit);
    strcpy(string60, "");
    itos(string15, zeit.hour, 2);
    strcat(string60, string15);
    strcat(string60, ":");
    itos(string15, zeit.min, 2);
    strcat(string60, string15);
    strcat(string60, ":");
    itos(string15, zeit.sec, 2);
    strcat(string60, string15);
    Serial.print(string60);
    //gibt Datum aus
    Serial.print(" D:");
    strcpy(string60, "");
    itos(string15, zeit.day, 2);
    strcat(string60, string15);
    strcat(string60, ".");
    itos(string15, zeit.month, 2);
    strcat(string60, string15);
    strcat(string60, ".");
    ltos(string15, zeit.year, 4);
    strcat(string60, string15);
    Serial.print(string60);
    //   Serial.print("\n");
    old_sec = zeit.sec;

    //Ausgabe ders Virtualisierungs-Mappings
    Serial.print(" V:");
    strcpy(string60, "");
    print_nibble(v_keys, 3);
    print_nibble(v_keys, 2);
    print_nibble(v_keys, 1);
    print_nibble(v_keys, 0);
    Serial.print(string60);
    Serial.print(" ->");
    //Ausgabe der Timer
    Serial.print(" T:");
    strcpy(string60, "");
    print_nibble(v_keys_timer, 3);
    print_nibble(v_keys_timer, 2);
    print_nibble(v_keys_timer, 1);
    print_nibble(v_keys_timer, 0);
    Serial.print(string60);
    Serial.print(" ->");
    strcpy(string60, "");
    //Ausgabe der Output-PINs
    Serial.print(" O:");
    print_nibble(output_soll, 3);
    print_nibble(output_soll, 2);
    print_nibble(output_soll, 1);
    print_nibble(output_soll, 0);
    Serial.print(string60);
    // abschlussverarbeitung
    Serial.print(" S: ");
    strcpy(string60, "");
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print("R");
      Serial.print(i);
      Serial.print(": ");
      Serial.print(rol_pos[i]);
      Serial.print("; ");
  }

  real_keys = 65535;
  long_pressed = 0;
  both_pressed = 0;
  both_double_clicked =0;
  single_click = 0;
  double_click = 0;
  both_double_clicked =0;
    Serial.print("\n");
  }
  //soll=ist

  //Zuweisen der Ausg‰nge abh. von Tasten
  output_soll = keys2relais(v_keys_timer);

  //Relais-Schaltung ausf¸hren incl. Schonung der Relais
  switch_relais(output_soll, output_ist);

  output_ist = output_soll;
  v_keys_old = v_keys;
//  real_keys = 65535;
//  long_pressed = 0;
//  both_pressed = 0
//both_double_clicked =0;
}

//      if (double_click[i]) {
//        doppel_klick &= ~(1 << i);
      //}

/*
 Z:00:00:06 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 0; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:07 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 0; R4: 0; R5: 0; R6: 0; R7: 0; 
 single_click1 128 single_click!3 128mod04 
 Z:00:00:08 D:00.00.0000 V:1111111101111111 -> T:1111111101111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 4; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:08 D:00.00.0000 V:1111111111111111 -> T:1111111101111111 -> O:0000000011000000 S: R0: 0; R1: 0; R2: 0; R3: 4; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:09 D:00.00.0000 V:1111111111111111 -> T:1111111101111111 -> O:0000000011000000 S: R0: 0; R1: 0; R2: 0; R3: 4; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:10 D:00.00.0000 V:1111111111111111 -> T:1111111101111111 -> O:0000000011000000 S: R0: 0; R1: 0; R2: 0; R3: 4; R4: 0; R5: 0; R6: 0; R7: 0; 
 single_click1 64 single_click!2 64mod03  Z:00:00:11 D:00.00.0000 V:1111111110111111 -> T:1111111111111111 -> O:0000000011000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:11 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:12 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 single_click1 64 single_click!2 64mod04 
 Z:00:00:12 D:00.00.0000 V:1111111110111111 -> T:1111111110111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 1; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:13 D:00.00.0000 V:1111111111111111 -> T:1111111110111111 -> O:0000000001000000 S: R0: 0; R1: 0; R2: 0; R3: 1; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:14 D:00.00.0000 V:1111111111111111 -> T:1111111110111111 -> 


 Z:00:00:35 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 2; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:36 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 2; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:37 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 2; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:38 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 2; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:39 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 2; R4: 0; R5: 0; R6: 0; R7: 0; 
 single_click1 6464 single_click2 0 single_click1 128 BP 64 H16 mod04 
 single_click!3 128!mod03  Z:00:00:39 D:00.00.0000 V:1111111101111111 -> T:1111111111111111 -> O:0000000001000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:40 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:41 D:00.00.0000 V:1111111111111111 -> T:1111111111111111 -> O:0000000000000000 S: R0: 0; R1: 0; R2: 0; R3: 128; R4: 0; R5: 0; R6: 0; R7: 0; 
 Z:00:00:42 D:00.00.0000 V:111

 
 8:57:46 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 Z:18:57:47 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 Z:18:57:48 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 Z:18:57:49 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 single_click1 20481024 single_click3 0 single_click1 1024 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  Z:18:57:50 D:06.08.2017 V:1111111111111111 -> T:1111111111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 128; R6: 2; R7: 0; 
 BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 Z:18:57:51 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000000000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  Z:18:57:52 D:06.08.2017 V:1111111111111111 -> T:1111111111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 128; R6: 2; R7: 0; 
 BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 Z:18:57:53 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000000000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  Z:18:57:54 D:06.08.2017 V:1111111111111111 -> T:1111111111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 128; R6: 2; R7: 0; 
 BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 Z:18:57:55 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000000000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  Z:18:57:56 D:06.08.2017 V:1111111111111111 -> T:1111111111111111 -> O:0000010000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 128; R6: 2; R7: 0; 
 BP 1024 HOben mod04 
 BP 1024 HOben old0 mod02  BP 1024 HOben mod04 
 Z:18:57:57 D:06.08.2017 V:1111111111111111 -> T:1111101111111111 -> O:0000000000000000 S: R0: 2; R1: 2; R2: 2; R3: 2; R4: 2; R5: 1; R6: 2; R7: 0; 
 BP
 */

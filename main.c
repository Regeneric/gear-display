#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include <util/delay.h>
#include <stdlib.h>


#define NEUTRAL 7   // Bieg neutralny

#define SEG_A PD0   // Segment "A"
#define SEG_B PD1   // Segment "B"
#define SEG_C PD2   // Segment "C"
#define SEG_D PD3   // Segment "D"
#define SEG_E PD4   // Segment "E"
#define SEG_F PD5   // Segment "F"
#define SEG_G PD6   // Segment "G"

#define LOCK_BUTTON PB1     // Przycisk blokujący wyświetlacz
#define LED_SENSOR  PB2     // Dioda biegu neutralnego
#define REVERSE_BTN PB3     // Przycisk odwracający logikę wyświetlacza

#define HALL_SENSOR_UP   PB4    // Górny czujnik Halla
#define HALL_SENSOR_DOWN PB5    // Dolny czujnik Halla


void zeruj()    {PORTD = 0x00;}
void wypelnij() {PORTD = 0xFF;}

void start();
void init();

void wyswietlacz(int cyfra);


int BIEG = NEUTRAL;
int BLOKADA = 0;
int ODWROCONY = 0;

int main() {    
    // Przerwania (IRQ) systemowe do odczytu czujników
    PCICR |= (1<<PCIE0);  // Ustawim PCIE0, żeby skanować maskę PCMSK0 (PCINT7..0)
    PCIFR |= (1<<PCIF0);  // Ustawiam PCIF0 żeby włączyć przerwania dla maski PCMSK0
    PCMSK0 = 0b00111110;  // Ustawiam PCINT1..5 jako porty wywołujące przerwanie 

    // Konfiguracja pinów jako wejścia i wyjścia
    // Piny wyjściowe
    PORTD = 0b01111111;   // Piny PD0 do PD6 (SEG_A do SEG_G) jako output


    // Piny wejściowe
    DDRB &= ~((1<<HALL_SENSOR_UP) | (1<<HALL_SENSOR_DOWN));
    DDRB &= ~((1<<LOCK_BUTTON) | (1<<LED_SENSOR) | (1<<REVERSE_BTN));

    PORTB |= ((1<<HALL_SENSOR_UP) | (1<<HALL_SENSOR_DOWN));             // Software'owe podciągnięcie rezystorem do +5V (pullup)
    PORTB |= ((1<<LOCK_BUTTON) | (1<<LED_SENSOR) | (1<<REVERSE_BTN));   // Software'owe podciągnięcie rezystorem do +5V (pullup)

    init();
    wyswietlacz(NEUTRAL);       // Przekazanie do funkcji `wyswietlacz()` aktualnej cyfry.
                                // Jako, że motocykl uruchamiany jest zawsze na biegu jałowym,
                                // to po sekwencji startowej nastąpi wyświetlenie litery 'n'

    sei();      // Włączenie globalnych przerwań (IRQ)
    while(1) {  // Pętla nieskończona - odpowiada za ciągłe działanie programu - odpowednik `void loop(){}`
        if(BLOKADA);

        wyswietlacz(BIEG);
        _delay_ms(500);
    } return 0;
}


void start() {
    int port = 1;
    for(int i = 0; i < 7; i++) {
        PORTD = port;
        port *= 2;
        _delay_ms(200);
    }
}

void init() {
    zeruj(); 
    start(); 
    zeruj();
    _delay_ms(200);
    wypelnij(); 
    _delay_ms(200);
    zeruj();

    return;
}

void wyswietlacz(int cyfra) {
    switch(cyfra) {
        case 1: PORTD = 0b00000110; break;
        case 2: PORTD = 0b01011011; break;
        case 3: PORTD = 0b01001111; break;
        case 4: PORTD = 0b01100110; break;
        case 5: PORTD = 0b01101101; break;
        case 6: PORTD = 0b01111101; break;
        case NEUTRAL: PORTD = 0b01010100; break;
        default: PORTD = 0b01110110; break;
    }
}

void biegGora() {
    if(BIEG == NEUTRAL) BIEG = 2;
    if(BIEG < 6) BIEG++;
}

void biegDol() {
    if(BIEG == NEUTRAL) BIEG = 1;
    if(BIEG > 1) BIEG++;
}


// Przerwanie dla LED_SENSOR, HALL_UP oraz HALL_DOWN
ISR(PCINT0_vect) {
    // Fragment kodu odpowiadający za blokadę wyświetlacza
    // utoworzny po to aby wyeliminować ewntualne błędne
    // naliczanie cyfr (przykładowo wtedy kiedy nie zazębił się
    // dany BIEG a dźwignia wykonałaby ruch aktwująć Hallotron)
    // nie będzie wtedy trzeba czekać na wyłączenie zasilania
    // tylko użytkownik aktywuje przycisk a program poczeka
    // na punkt referencyjny - BIEG neutralny i od tego momentu
    // będzie kontynuował swoje działanie

    if(!(PINB & (1<<LOCK_BUTTON))) BLOKADA = 1;
    else BLOKADA = 0; 


    // Jeśli aktywowano przycisk odwracający zasadę działania wyświetlacza
    // to wtedy zmienna typu logicznego zmieni swoją wartość na przeciwną
    if(!(PINB & (1<<REVERSE_BTN))) ODWROCONY = !ODWROCONY;


    if(!(PINB & (1<<LED_SENSOR))) BIEG = NEUTRAL;
    if(!(PINB & (1<<HALL_SENSOR_UP))) ODWROCONY ? biegDol() : biegGora();
    if(!(PINB & (1<<HALL_SENSOR_DOWN))) ODWROCONY ? biegGora() : biegDol();
}
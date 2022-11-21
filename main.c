#include <avr/io.h>
#include <avr/interrupt.h>

#include <util/delay.h>
#include <stdlib.h>


// NAZWA          | PORTY ARDUINO | PORTY ATMEGA
// ---------------|---------------|-------------
// SEG_A do SEG_G | D0 do D6      | PD0 do PD6
// LOCK_BUTTON    | D9            | PB1
// LED_SENSOR     | D10           | PB2
// REVERSE_BTN    | D11           | PB3
// HALL_UP        | D12           | PB4
// HALL_DOWN      | D13           | PB5


// #define potrzebne do ośmiobitowego zegara i funkcji millis(), zamist _delay_ms()
#define clockCyclesToMicroseconds(a) (((a) * 1000L)/(F_CPU / 1000L))
#define MICROSECONDS_PER_TIMER0_OVERFLOW (clockCyclesToMicroseconds(64 * 256))
#define MILLIS_INC (MICROSECONDS_PER_TIMER0_OVERFLOW / 1000)
#define FRACT_INC ((MICROSECONDS_PER_TIMER0_OVERFLOW % 1000)>>3)
#define FRACT_MAX (1000>>3)

volatile unsigned long int zeroOverflowCount = 0;
volatile unsigned long int zeroMillis = 0;
static unsigned char zeroFract = 0;
unsigned long int startTime = 0, endTime = 0;

unsigned long int millis();


#define MAX_GEAR 6          // Ilość biegów w skrzyni
#define NEUTRAL  MAX_GEAR+1 // Bieg neutralny

#define LOCK_BUTTON PB1     // Przycisk blokujący wyświetlacz
#define LED_SENSOR  PB2     // Dioda biegu neutralnego
#define REVERSE_BTN PB3     // Przycisk odwracający logikę wyświetlacza

#define HALL_SENSOR_UP   PB4    // Górny czujnik Halla
#define HALL_SENSOR_DOWN PB5    // Dolny czujnik Halla


void wait(int m);

void zeruj()    {PORTD = 0x00;}
void wypelnij() {PORTD = 0xFF;}

void start();
void init();

void wyswietlacz(int cyfra);


int gBieg = NEUTRAL;
int gBlokada = 0;
int gOdwrocony = 0;

int main() {
    // Ośmiobitowy timer dla funkcji millis()
    TCCR0B |= ((1<<CS01) | (1<<CS00));  // Prescaler 64
    TIMSK0 = (1<<TOIE0);                // Overflow IRQ
    TCNT0 = 0;                          // Liczy 0 do 255

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
        wyswietlacz(gBieg);
        _delay_ms(500);
    } return 0;
}


void wait(int m) {
    // TODO
    return;
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
        case NEUTRAL: PORTD = 0b01010100; break;
        case 1: PORTD = 0b00000110; break;
        case 2: PORTD = 0b01011011; break;
        case 3: PORTD = 0b01001111; break;
        case 4: PORTD = 0b01100110; break;
        case 5: PORTD = 0b01101101; break;
        case 6: PORTD = 0b01111101; break;

        default: PORTD = 0b01110110; break;
    }
}

void biegGora() {
    if(gBieg == NEUTRAL) gBieg = 2;
        else if(gBieg < MAX_GEAR) gBieg++;
    else return;
}

void biegDol() {
    if(gBieg == NEUTRAL) gBieg = 1;
        else if(gBieg == 2) gBieg = NEUTRAL;
        else if(gBieg > 1) gBieg--;
    else return;
}


unsigned long int millis() {
    unsigned long int m = 0;
    uint8_t oldSREG = SREG;

    cli();  // Wyłączm przerwania na czas pomiaru
        m = zeroMillis;
        SREG = oldSREG;
    sei();  // Potem włączam je ponownie

    return m;
}



ISR(TIMER0_OVF_vect) {
    unsigned long int m = zeroMillis;
    unsigned char f = zeroFract;

    m += MILLIS_INC;
    f += FRACT_INC;
    if(f >= FRACT_MAX) {
        f -= FRACT_MAX;
        m += 1;
    }

    zeroFract = f;
    zeroMillis = m;
    zeroOverflowCount++;
}

// Przerwanie dla LED_SENSOR, HALL_UP oraz HALL_DOWN
ISR(PCINT0_vect) {
    // Fragment kodu odpowiadający za blokadę wyświetlacza
    // utoworzny po to aby wyeliminować ewntualne błędne
    // naliczanie cyfr (przykładowo wtedy kiedy nie zazębił się
    // dany gBieg a dźwignia wykonałaby ruch aktwująć Hallotron)
    // nie będzie wtedy trzeba czekać na wyłączenie zasilania
    // tylko użytkownik aktywuje przycisk a program poczeka
    // na punkt referencyjny - gBieg neutralny i od tego momentu
    // będzie kontynuował swoje działanie

    if(!(PINB & (1<<LOCK_BUTTON))) gBlokada = 1;
    else gBlokada = 0; 

    // Jeśli aktywowano przycisk odwracający zasadę działania wyświetlacza
    // to wtedy zmienna typu logicznego zmieni swoją wartość na przeciwną
    if(!(PINB & (1<<REVERSE_BTN))) gOdwrocony = !gOdwrocony;


    if(!(PINB & (1<<LED_SENSOR))) gBieg = NEUTRAL;
    
    if(!(PINB & (1<<HALL_SENSOR_UP))) {
        if(!gBlokada) gOdwrocony ? biegDol() : biegGora();
        else return;
    }

    if(!(PINB & (1<<HALL_SENSOR_DOWN))) {
        if(!gBlokada) gOdwrocony ? biegGora() : biegDol();
        else return;
    }
}
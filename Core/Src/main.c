/*
 * ADASmini - STM32F401RE, register-level
 * HC-SR04 (PB8 TRIG / PB9 ECHO), L298N (PC0, PC1), SSD1306 I2C1 (PB6 SCL / PB7 SDA),
 * LEDs (PA4 DANGER / PA5 WARNING).
 * Assumes the default 16 MHz HSI clock (as in the original code).
 */
#include <main.h>

/* ---------------- Configuration ---------------- */
#define DANGER_CM        50
#define WARNING_CM       100
#define HYST_CM          5        /* hysteresis to stop flicker near thresholds */
#define CYCLE_US         60000u   /* HC-SR04 needs >= ~60 ms between measurements */
#define BACKOFF_DELAY_US 100000u  /* stop time before the back-off action */
#define ERR_LIMIT        3        /* consecutive sensor errors before ERROR state */

#define ECHO_RISE_TIMEOUT_US 5000u   /* sensor missing / not responding */
#define ECHO_FALL_TIMEOUT_US 45000u  /* HC-SR04 max echo is ~38 ms (no target) */
#define I2C_TIMEOUT_US       5000u

#define OLED_I2C_FAST    0        /* 1 = 400 kHz (needs good external pull-ups ~4.7k) */
#define OLED_I2C_ADDR    0x78

#define ERR_NO_ECHO      (-1)
#define ERR_ECHO_STUCK   (-2)

typedef enum { MOTOR_STOP = 0, MOTOR_BACKOFF = 1, MOTOR_RUN = 3 } MotorMode_t;
typedef enum { ST_SAFE = 0, ST_WARNING, ST_DANGER, ST_ERROR } State_t;

volatile int32_t front_dis_cm = 0;

/* ---------------- Timebase (TIM2, 32-bit, 1 MHz free-running) ---------------- */
static inline uint32_t micros(void) { return TIM2->CNT; }

static void delay_us(uint32_t us) {
    uint32_t t0 = micros();
    while ((micros() - t0) < us);
}

static void Timebase_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->PSC = 15;               /* 16 MHz / 16 = 1 MHz */
    TIM2->ARR = 0xFFFFFFFFu;      /* free-running: wraps after ~71 min, handled by unsigned subtraction */
    TIM2->EGR = TIM_EGR_UG;       /* load PSC now (it is buffered until an update event) */
    TIM2->CR1 |= TIM_CR1_CEN;
}

/* ---------------- HC-SR04 ---------------- */
static void HCSR05_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    /* PB8 TRIG: output, push-pull, high speed, no pull */
    GPIOB->MODER   = (GPIOB->MODER   & ~(3u << 16)) | (1u << 16);
    GPIOB->OTYPER &= ~(1u << 8);
    GPIOB->OSPEEDR |= (3u << 16);
    GPIOB->PUPDR  &= ~(3u << 16);
    /* PB9 ECHO: input with pull-down so a disconnected sensor reads as "no echo" */
    GPIOB->MODER  &= ~(3u << 18);
    GPIOB->PUPDR   = (GPIOB->PUPDR & ~(3u << 18)) | (2u << 18);
}


static int32_t getDis(void) {
    GPIOB->BSRR = (1u << 8);
    delay_us(10);
    GPIOB->BSRR = (1u << (8 + 16));

    uint32_t t0 = micros();
    while (!(GPIOB->IDR & (1u << 9))) {
        if ((micros() - t0) > ECHO_RISE_TIMEOUT_US) return ERR_NO_ECHO;
    }
    uint32_t start = micros();
    while (GPIOB->IDR & (1u << 9)) {
        if ((micros() - start) > ECHO_FALL_TIMEOUT_US) return ERR_ECHO_STUCK;
    }
    uint32_t duration = micros() - start;   /* us; unsigned maths handles wrap-around */
    return (int32_t)(duration / 58u);       /* us -> cm */
}

/* ---------------- Motors (L298N) ---------------- */
static void Motor_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER = (GPIOC->MODER & ~((3u << 0) | (3u << 2))) | (1u << 0) | (1u << 2);
    GPIOC->BSRR  = (1u << (0 + 16)) | (1u << (1 + 16));
}

static void Motor_Control(MotorMode_t mode) {
    switch (mode) {
        case MOTOR_BACKOFF: GPIOC->BSRR = (1u << 1) | (1u << (0 + 16)); break; /* PC1=1, PC0=0 */
        case MOTOR_RUN:     GPIOC->BSRR = (1u << 1) | (1u << 0);        break; /* both high   */
        case MOTOR_STOP:
        default:            GPIOC->BSRR = (1u << (0 + 16)) | (1u << (1 + 16)); break;
    }
}

/* ---------------- LEDs ---------------- */
static void LED_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 8) | (3u << 10))) | (1u << 8) | (1u << 10);
    GPIOA->BSRR  = (1u << (4 + 16)) | (1u << (5 + 16));
}
static void LED_Set(int danger, int warning) {
    GPIOA->BSRR = danger  ? (1u << 4) : (1u << (4 + 16));
    GPIOA->BSRR = warning ? (1u << 5) : (1u << (5 + 16));
}

/* ---------------- I2C1 + SSD1306 ---------------- */
static uint8_t oled_ok = 1;
uint8_t OLED_Buffer[1024];

static const uint8_t Font5x7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    [','] = {0x00, 0x07, 0x06, 0x00, 0x00},
    ['A'] = {0x7C, 0x12, 0x11, 0x12, 0x7C},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F}
};
#define FONT_LEN (sizeof(Font5x7) / sizeof(Font5x7[0]))

static void I2C1_Register_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    GPIOB->MODER   = (GPIOB->MODER   & ~((3u << 12) | (3u << 14))) | (2u << 12) | (2u << 14);
    GPIOB->OTYPER |= (1u << 6) | (1u << 7);
    GPIOB->OSPEEDR |= (3u << 12) | (3u << 14);
    GPIOB->PUPDR   = (GPIOB->PUPDR & ~((3u << 12) | (3u << 14))) | (1u << 12) | (1u << 14);
    GPIOB->AFR[0]  = (GPIOB->AFR[0] & ~((0xFu << 24) | (0xFu << 28))) | (4u << 24) | (4u << 28);

    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
    I2C1->CR2 = (I2C1->CR2 & ~I2C_CR2_FREQ) | 16;
#if OLED_I2C_FAST
    I2C1->CCR   = I2C_CCR_FS | 14;   /* ~380 kHz at PCLK1 = 16 MHz */
    I2C1->TRISE = 5;
#else
    I2C1->CCR   = 80;                /* 100 kHz */
    I2C1->TRISE = 17;
#endif
    I2C1->CR1 |= I2C_CR1_PE;
}


static int i2c_wait_set(volatile uint32_t *reg, uint32_t mask) {
    uint32_t t0 = micros();
    while (!(*reg & mask)) {
        if ((micros() - t0) > I2C_TIMEOUT_US) return 0;
    }
    return 1;
}

static int I2C1_BurstWrite(uint8_t slave_addr, uint8_t control_byte, const uint8_t *data, uint16_t size) {
    int ok = 0;

    I2C1->CR1 |= I2C_CR1_START;
    if (!i2c_wait_set(&I2C1->SR1, I2C_SR1_SB)) goto fail;

    I2C1->DR = slave_addr;
    if (!i2c_wait_set(&I2C1->SR1, I2C_SR1_ADDR)) goto fail;
    (void)I2C1->SR1;
    (void)I2C1->SR2;

    if (!i2c_wait_set(&I2C1->SR1, I2C_SR1_TXE)) goto fail;
    I2C1->DR = control_byte;

    for (uint16_t i = 0; i < size; i++) {
        if (!i2c_wait_set(&I2C1->SR1, I2C_SR1_TXE)) goto fail;
        I2C1->DR = data[i];
    }
    if (!i2c_wait_set(&I2C1->SR1, I2C_SR1_BTF)) goto fail;
    ok = 1;

fail:
    I2C1->CR1 |= I2C_CR1_STOP;
    I2C1->SR1 &= ~I2C_SR1_AF;
    {
        uint32_t t0 = micros();
        while ((I2C1->CR1 & I2C_CR1_STOP) && (micros() - t0) < I2C_TIMEOUT_US);
    }
    if (!ok) {
        I2C1->CR1 |= I2C_CR1_SWRST;
        I2C1->CR1 &= ~I2C_CR1_SWRST;
        I2C1->CR1 |= I2C_CR1_PE;
    }
    return ok;
}

static void OLED_Init(void) {
    static const uint8_t init_seq[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40, 0x81, 0x7F, 0xA1, 0xA6,
        0xA8, 0x3F, 0xA4, 0xD3, 0x00, 0xD5, 0x80, 0xD9, 0x22, 0xDA, 0x12,
        0xDB, 0x20, 0x8D, 0x14, 0xAF
    };

    oled_ok = (uint8_t)I2C1_BurstWrite(OLED_I2C_ADDR, 0x00, init_seq, sizeof(init_seq));
}

static void OLED_Clear(uint8_t pattern) {
    for (uint16_t i = 0; i < sizeof(OLED_Buffer); i++) OLED_Buffer[i] = pattern;
}

static void OLED_DrawChar_2X(uint8_t x, uint8_t page, char c) {
    if (x > 116 || page > 6) return;
    uint8_t idx = (uint8_t)c;
    if (idx >= FONT_LEN) idx = ' ';

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t src = Font5x7[idx][i];
        uint8_t top = 0, bottom = 0;
        for (uint8_t bit = 0; bit < 4; bit++) {
            if (src & (1u << bit))       top    |= (uint8_t)(3u << (bit * 2));
            if (src & (1u << (bit + 4))) bottom |= (uint8_t)(3u << (bit * 2));
        }
        uint16_t t = (uint16_t)page * 128 + x + i * 2;
        uint16_t b = (uint16_t)(page + 1) * 128 + x + i * 2;
        OLED_Buffer[t] = OLED_Buffer[t + 1] = top;
        OLED_Buffer[b] = OLED_Buffer[b + 1] = bottom;
    }
    uint16_t t = (uint16_t)page * 128 + x + 10;
    uint16_t b = (uint16_t)(page + 1) * 128 + x + 10;
    OLED_Buffer[t] = OLED_Buffer[t + 1] = 0x00;
    OLED_Buffer[b] = OLED_Buffer[b + 1] = 0x00;
}

static void OLED_DrawString_2X(uint8_t x, uint8_t page, const char *str) {
    while (*str) {
        OLED_DrawChar_2X(x, page, *str++);
        x += 12;
    }
}


static void OLED_Update(void) {
    if (!oled_ok) return;
    static const uint8_t cmds[] = { 0x21, 0, 127, 0x22, 3, 4 };
    if (!I2C1_BurstWrite(OLED_I2C_ADDR, 0x00, cmds, sizeof(cmds))) return;
    I2C1_BurstWrite(OLED_I2C_ADDR, 0x40, &OLED_Buffer[3 * 128], 2 * 128);
}

static void OLED_ShowState(State_t s) {
    OLED_Clear(0x00);
    switch (s) {
        case ST_DANGER:  OLED_DrawString_2X(29, 3, "DANGER");  break;
        case ST_WARNING: OLED_DrawString_2X(23, 3, "WARNING"); break;
        case ST_ERROR:   OLED_DrawString_2X(46, 3, "ERR");     break;
        default:         OLED_DrawString_2X(29, 3, "SAFE");    break;
    }
    OLED_Update();
}

/* ---------------- Logic ---------------- */
static State_t classify(int32_t d, State_t prev) {
    int32_t danger_th  = DANGER_CM  + (prev == ST_DANGER  ? HYST_CM : 0);
    int32_t warning_th = WARNING_CM + (prev == ST_WARNING ? HYST_CM : 0);
    if (d < danger_th)  return ST_DANGER;
    if (d < warning_th) return ST_WARNING;
    return ST_SAFE;
}

static void apply_state(State_t s) {
    switch (s) {
        case ST_DANGER:  LED_Set(1, 0); Motor_Control(MOTOR_STOP); break;
        case ST_WARNING: LED_Set(0, 1); Motor_Control(MOTOR_RUN);  break;
        case ST_ERROR:   LED_Set(1, 1); Motor_Control(MOTOR_STOP); break; /* fail-safe */
        default:         LED_Set(0, 0); Motor_Control(MOTOR_RUN);  break;
    }
    OLED_ShowState(s);
}

int main(void) {
    Timebase_Init();
    HCSR05_Init();
    Motor_Init();
    LED_Init();
    I2C1_Register_Init();
    OLED_Init();

    State_t state = ST_SAFE;
    apply_state(state);

    uint8_t  err_count = 0;
    uint8_t  backoff_done = 0;
    uint32_t danger_since = 0;

    while (1) {
        uint32_t t_cycle = micros();

        int32_t d = getDis();
        front_dis_cm = d;

        State_t next = state;
        if (d < 0) {
            if (++err_count >= ERR_LIMIT) { next = ST_ERROR; err_count = ERR_LIMIT; }
        } else {
            err_count = 0;
            next = classify(d, state);
        }

        if (next != state) {
            state = next;
            apply_state(state);
            if (state == ST_DANGER) { danger_since = micros(); backoff_done = 0; }
        }


        if (state == ST_DANGER && !backoff_done &&
            (micros() - danger_since) >= BACKOFF_DELAY_US) {
            Motor_Control(MOTOR_BACKOFF);
            backoff_done = 1;
        }

        while ((micros() - t_cycle) < CYCLE_US);
    }
}

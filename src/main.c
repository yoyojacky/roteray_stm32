/*
 * STM32F103C8T6 rotary encoder test.
 *   CLK -> PA6, DT -> PA4, SW -> PA7 (all inputs with internal pull-up)
 *   USART1: PA9=TX, PA10=RX, 115200 8N1
 * Prints encoder direction/count and button state over UART.
 */
#include <stdint.h>

typedef struct {
  volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_TypeDef;

typedef struct {
  volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR,
                    APB1ENR, BDCR, CSR;
} RCC_TypeDef;

typedef struct {
  volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_TypeDef;

typedef struct {
  volatile uint32_t CTRL, LOAD, VAL, CALIB;
} SysTick_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)  0x40010800UL)
#define RCC     ((RCC_TypeDef *)   0x40021000UL)
#define USART1  ((USART_TypeDef *) 0x40013800UL)
#define SYSTICK ((SysTick_TypeDef *)0xE000E010UL)
#define FLASH_ACR (*(volatile uint32_t *)0x40022000UL)

#define PIN_DT   4   /* PA4  */
#define PIN_CLK  6   /* PA6  */
#define PIN_SW   7   /* PA7  */

/* Set to 1 if CW/CCW come out swapped for your encoder */
#define ENCODER_REVERSE  0

static volatile uint32_t ms_ticks;
static volatile int8_t   enc_step;    /* +1 one CW detent, -1 one CCW detent */
static volatile int32_t  enc_count;
static volatile uint8_t  sw_state;    /* 1 = pressed */
static volatile uint8_t  sw_event;    /* 0 none, 1 pressed, 2 released */

/* ------------------------------------------------------------------ */
/* UART helpers                                                        */
/* ------------------------------------------------------------------ */
static void uart_putc(char c)
{
  while (!(USART1->SR & (1u << 7))) { }   /* wait TXE */
  USART1->DR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
  while (*s) {
    if (*s == '\n') uart_putc('\r');
    uart_putc(*s++);
  }
}

static void uart_putint(int32_t v)
{
  char buf[12];
  uint32_t u;
  int i = 0;
  if (v < 0) { uart_putc('-'); u = (uint32_t)(-v); }
  else       { u = (uint32_t)v; }
  do {
    buf[i++] = (char)('0' + (u % 10));
    u /= 10;
  } while (u);
  while (i--) uart_putc(buf[i]);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
static void clock_init(void)
{
  RCC->CR |= (1u << 16);                    /* HSE on */
  while (!(RCC->CR & (1u << 17))) { }       /* wait HSERDY */

  FLASH_ACR = (1u << 4) | 2u;               /* prefetch on, latency 2 */

  /* PLL = HSE(8MHz) x 9 = 72MHz, AHB/1, APB1/2, APB2/1 */
  RCC->CFGR = (1u << 16) | (7u << 18) | (4u << 8) | (0u << 11);
  RCC->CR |= (1u << 24);                    /* PLL on */
  while (!(RCC->CR & (1u << 25))) { }       /* wait PLLRDY */

  RCC->CFGR |= 2u;                          /* SYSCLK = PLL */
  while ((RCC->CFGR & 0xCu) != 0x8u) { }
}

static void gpio_init(void)
{
  RCC->APB2ENR |= (1u << 2) | (1u << 14);   /* IOPAEN + USART1EN */

  /* PA4/PA6/PA7: input with pull-up (CNF=10, MODE=00) */
  GPIOA->CRL &= ~((0xFu << 16) | (0xFu << 24) | (0xFu << 28));
  GPIOA->CRL |=  ((0x8u << 16) | (0x8u << 24) | (0x8u << 28));
  GPIOA->ODR |= (1u << PIN_DT) | (1u << PIN_CLK) | (1u << PIN_SW);

  /* PA9: AF push-pull 50MHz (CNF=10, MODE=11); PA10: floating input (default) */
  GPIOA->CRH &= ~(0xFu << 4);
  GPIOA->CRH |=  (0xBu << 4);
}

static void usart_init(void)
{
  USART1->BRR = 625;                        /* 72MHz / 115200 */
  USART1->CR1 = (1u << 13) | (1u << 3) | (1u << 2);  /* UE, TE, RE */
}

static void systick_init(void)
{
  SYSTICK->LOAD = 72000u - 1u;              /* 1ms @ 72MHz */
  SYSTICK->VAL  = 0;
  SYSTICK->CTRL = (1u << 2) | (1u << 1) | 1u;  /* CPU clk, irq, enable */
}

/* ------------------------------------------------------------------ */
/* 1ms polling: quadrature decoder + button debounce                   */
/* ------------------------------------------------------------------ */
void SysTick_Handler(void)
{
  /* valid-transition table, index = (prev CLK,DT << 2) | (curr CLK,DT) */
  static const int8_t qem[16] = {
    0, -1, 1, 0,
    1,  0, 0, -1,
   -1,  0, 0, 1,
    0,  1, -1, 0
  };
  static uint8_t enc_prev;
  static int8_t  enc_acc;
  static uint8_t btn_cnt;

  ms_ticks++;

  uint32_t idr = GPIOA->IDR;
  uint8_t curr = (uint8_t)((((idr >> PIN_CLK) & 1u) << 1) | ((idr >> PIN_DT) & 1u));
  uint8_t idx  = (uint8_t)((enc_prev << 2) | curr);
  enc_prev = curr;
  enc_acc = (int8_t)(enc_acc + qem[idx]);
  if (enc_acc >= 4) {
    enc_acc = 0;
    enc_step = ENCODER_REVERSE ? -1 : 1;
  } else if (enc_acc <= -4) {
    enc_acc = 0;
    enc_step = ENCODER_REVERSE ? 1 : -1;
  }

  uint8_t raw = (idr & (1u << PIN_SW)) ? 0u : 1u;   /* active low */
  if (raw != sw_state) {
    if (++btn_cnt >= 20u) {                 /* stable for 20ms */
      sw_state = raw;
      sw_event = raw ? 1u : 2u;
      btn_cnt = 0;
    }
  } else {
    btn_cnt = 0;
  }
}

/* ------------------------------------------------------------------ */
int main(void)
{
  clock_init();
  gpio_init();
  usart_init();
  systick_init();

  uart_puts("\n\n=============================\n");
  uart_puts("STM32F103 Encoder Test ready\n");
  uart_puts("CLK=PA6 DT=PA4 SW=PA7\n");
  uart_puts("UART: 115200 8N1 on PA9(TX)\n");
  uart_puts("=============================\n");

  uint32_t last_hb = 0;
  for (;;) {
    if (enc_step) {
      int8_t s;
      __asm volatile ("cpsid i");
      s = enc_step; enc_step = 0;
      __asm volatile ("cpsie i");
      enc_count += s;
      uart_puts(s > 0 ? "Encoder: CW " : "Encoder: CCW");
      uart_puts("  count=");
      uart_putint(enc_count);
      uart_puts("\n");
    }

    if (sw_event) {
      uint8_t e;
      __asm volatile ("cpsid i");
      e = sw_event; sw_event = 0;
      __asm volatile ("cpsie i");
      uart_puts(e == 1u ? "Button: PRESSED\n" : "Button: RELEASED\n");
    }

    if (ms_ticks - last_hb >= 2000u) {
      last_hb = ms_ticks;
      uart_puts("[alive] count=");
      uart_putint(enc_count);
      uart_puts("  SW=");
      uart_putint(sw_state);
      uart_puts("\n");
    }
  }
}

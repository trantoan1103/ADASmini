#include <main.h>

float front_dis = 0;

void delay_us(uint32_t us){TIM2->CNT = 0;while(TIM2->CNT < us);}

void HCSR05_Init(){

RCC->AHB1ENR |= 1 << 1;
//PB9 -> ECHO PB8 -> TRIG
GPIOB->MODER &= ~(1 << 16 | 1 << 17);
GPIOB->MODER |= 1 << 16;
GPIOB->OTYPER &= ~(1 << 8);
GPIOB->OSPEEDR |= (1 << 16 | 1 << 17);
GPIOB->PUPDR &= ~(1 << 16 | 1 << 17);

GPIOB->MODER &= ~(1 << 18 | 1 << 19);
GPIOB->PUPDR &= ~(1 << 18 | 1 << 19);

RCC->APB1ENR |= 1 << 0;

TIM2->PSC = 15;
TIM2->ARR = 999999;
TIM2->CR1 |= 1 << 0;

}

float getDis(){

uint32_t start = 0, end = 0;
uint32_t timeout = 1e7;

GPIOB->ODR |= 1 << 8;
delay_us(10);
GPIOB->ODR &= ~(1 << 8);

while (!(GPIOB->IDR & (1 << 9))){
	if (--timeout == 0) return -1.0f;
}

start = TIM2->CNT;

timeout = 1e7;
while (GPIOB->IDR & (1 << 9)){
	if (--timeout == 0) return -2.0f;
}

end = TIM2->CNT;

uint32_t duration = end > start ? end - start : 999999 - start + end + 1;

return (float)duration / 58.0f;

}

void delay_ms(uint32_t ms){for (uint32_t i = 0; i < ms;i++)for (uint32_t j = 0; j < 1600; j++);}

void Motor_Init(){//PC0->LEFT PC1->RIGHTRCC->AHB1ENR |= 1 << 2;

GPIOC->MODER &= ~(1 << 0 | 1 << 1 | 1 << 2 | 1 << 3);
GPIOC->MODER |= (1 << 2 | 1 << 0);

GPIOC->ODR &= ~(1 << 0 | 1 << 1);

}

void Motor_Control(int mode){

switch (mode){
	case 0: GPIOC->ODR &= ~(1 << 1 | 1 << 0); break;
	case 1: GPIOC->ODR |= 1 << 1; GPIOC->ODR &= ~(1 << 0); break;
	case 2: GPIOC->ODR |= 1 << 0; GPIOC->ODR &= ~(1 << 1); break;
	case 3: GPIOC->ODR |= (1 << 1 | 1 << 0); break;
	default:GPIOC->ODR &= ~(1 << 1 | 1 << 0); break;
}

}

#define OLED_I2C_ADDR       0x78

uint8_t OLED_Buffer[1024];

const uint8_t Font5x7[][5] = {[' '] = {0x00, 0x00, 0x00, 0x00, 0x00},[','] = {0x00, 0x07, 0x06, 0x00, 0x00},['A'] = {0x7C, 0x12, 0x11, 0x12, 0x7C},['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},['W'] = {0x7F, 0x20, 0x18, 0x20, 0x7F}};

void Delay_ms(uint32_t ms) {uint32_t count = ms * 2000;while (count--) {__NOP();}}

void I2C1_Register_Init(void) {RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

GPIOB->MODER   &= ~((3 << (6 * 2)) | (3 << (7 * 2)));
GPIOB->MODER   |=  ((2 << (6 * 2)) | (2 << (7 * 2)));
GPIOB->OTYPER  |= (1 << 6) | (1 << 7);
GPIOB->OSPEEDR |= ((3 << (6 * 2)) | (3 << (7 * 2)));
GPIOB->PUPDR   &= ~((3 << (6 * 2)) | (3 << (7 * 2)));
GPIOB->PUPDR   |=  ((1 << (6 * 2)) | (1 << (7 * 2)));

GPIOB->AFR[0] &= ~((0xF << (6 * 4)) | (0xF << (7 * 4)));
GPIOB->AFR[0] |=  ((4   << (6 * 4)) | (4   << (7 * 4)));

I2C1->CR1 |= I2C_CR1_SWRST;
I2C1->CR1 &= ~I2C_CR1_SWRST;
I2C1->CR2 &= ~I2C_CR2_FREQ;
I2C1->CR2 |= 16;
I2C1->CCR &= ~I2C_CCR_FS;
I2C1->CCR = 80;
I2C1->TRISE = 17;
I2C1->CR1 |= I2C_CR1_PE;
}

void I2C1_BurstWrite(uint8_t slave_addr, uint8_t control_byte, uint8_t *data, uint16_t size) {
    I2C1->CR1 |= I2C_CR1_START;
    while (!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = slave_addr;
    while (!(I2C1->SR1 & I2C_SR1_ADDR));

    (void)I2C1->SR1;
    (void)I2C1->SR2;

    while (!(I2C1->SR1 & I2C_SR1_TXE));
    I2C1->DR = control_byte;

    for (uint16_t i = 0; i < size; i++) {// gửi từng byte dữ liệu
        while (!(I2C1->SR1 & I2C_SR1_TXE));
        I2C1->DR = data[i];
        for(volatile uint8_t delay = 0; delay < 15; delay++);
    }

    while (!(I2C1->SR1 & I2C_SR1_BTF));
    I2C1->CR1 |= I2C_CR1_STOP;
}

void OLED_SendCommand(uint8_t cmd) {
    I2C1_BurstWrite(OLED_I2C_ADDR, 0x00, &cmd, 1);
}

void OLED_Init(void) {
    OLED_SendCommand(0xAE);
    OLED_SendCommand(0x20); OLED_SendCommand(0x00);
    OLED_SendCommand(0xB0);
    OLED_SendCommand(0xC8);
    OLED_SendCommand(0x00);
    OLED_SendCommand(0x10);
    OLED_SendCommand(0x40);
    OLED_SendCommand(0x81); OLED_SendCommand(0x7F);
    OLED_SendCommand(0xA1);
    OLED_SendCommand(0xA6);
    OLED_SendCommand(0xA8); OLED_SendCommand(0x3F);
    OLED_SendCommand(0xA4);
    OLED_SendCommand(0xD3); OLED_SendCommand(0x00);
    OLED_SendCommand(0xD5); OLED_SendCommand(0x80);
    OLED_SendCommand(0xD9); OLED_SendCommand(0x22);
    OLED_SendCommand(0xDA); OLED_SendCommand(0x12);
    OLED_SendCommand(0xDB); OLED_SendCommand(0x20);
    OLED_SendCommand(0x8D); OLED_SendCommand(0x14);
    OLED_SendCommand(0xAF);
}

void OLED_Clear(uint8_t pattern) {
    for (uint16_t i = 0; i < 1024; i++) {
        OLED_Buffer[i] = pattern;
    }
}

void OLED_DrawChar_2X(uint8_t x, uint8_t page, char c) {

    if (x > 118 || page > 6) return;

    if (c != ' ' && c != ',' && c != 'A' && c != 'G' && c != 'I' &&
        c != 'N' && c != 'R' && c != 'W' && c != 'S' && c != 'F' && c != 'E' && c != 'D') {
        c = ' ';
    }

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t byte_goc = Font5x7[(uint8_t)c][i];

        uint8_t byte_tren = 0;
        uint8_t byte_duoi = 0;

        for (uint8_t bit = 0; bit < 4; bit++) {
            if (byte_goc & (1 << bit)) {
                byte_tren |= (3 << (bit * 2));
            }
            if (byte_goc & (1 << (bit + 4))) {
                byte_duoi |= (3 << (bit * 2));
            }
        }

        uint16_t idx_page_tren = page * 128 + (x + i * 2);
        uint16_t idx_page_duoi = (page + 1) * 128 + (x + i * 2);

        OLED_Buffer[idx_page_tren]     = byte_tren;
        OLED_Buffer[idx_page_tren + 1] = byte_tren;

        OLED_Buffer[idx_page_duoi]     = byte_duoi;
        OLED_Buffer[idx_page_duoi + 1] = byte_duoi;
    }

    uint16_t space_tren = page * 128 + (x + 10);
    uint16_t space_duoi = (page + 1) * 128 + (x + 10);
    OLED_Buffer[space_tren]     = 0x00;
    OLED_Buffer[space_tren + 1] = 0x00;
    OLED_Buffer[space_duoi]     = 0x00;
    OLED_Buffer[space_duoi + 1] = 0x00;
    }

    void OLED_DrawString_2X(uint8_t x, uint8_t page, char *str) {
        while (*str) {
            OLED_DrawChar_2X(x, page, *str);
            x += 12;
            str++;
        }
    }

    void OLED_Update(void) {
        OLED_SendCommand(0x21); OLED_SendCommand(0); OLED_SendCommand(127);
        OLED_SendCommand(0x22); OLED_SendCommand(0); OLED_SendCommand(7);
        I2C1_BurstWrite(OLED_I2C_ADDR, 0x40, OLED_Buffer, 1024);
    }

    int main(){

    	HCSR05_Init();
    	Motor_Init();
    	I2C1_Register_Init();
        OLED_Init();

        RCC->AHB1ENR |= 1 << 0;

        GPIOA->MODER &= ~(1 << 9);
        GPIOA->MODER |= 1 << 8;
        GPIOA->ODR &= ~(1 << 4);

        GPIOA->MODER &= ~(1 << 11);
        GPIOA->MODER |= 1 << 10;
        GPIOA->ODR &= ~(1 << 5);

    	while(1){

    		front_dis = getDis();

    		if (front_dis < 50.0f){

    			GPIOA->ODR |= 1 << 4;
    			GPIOA->ODR &= ~(1 << 5);

    	        OLED_Clear(0x00);
    	        OLED_DrawString_2X(29, 3, "DANGER");
    	        OLED_Update();

    			Motor_Control(0);

    			delay_ms(100);

    			Motor_Control(1);

    		} else {
    			if (front_dis < 100.0f) {
    		        OLED_Clear(0x00);
    		        OLED_DrawString_2X(23, 3, "WARNING");
    		        OLED_Update();
    		        GPIOA->ODR |= 1 << 5;
    			} else {
    		        OLED_Clear(0x00);
    		        OLED_DrawString_2X(29, 3, "SAFE");
    		        OLED_Update();
    				GPIOA->ODR &= ~(1 << 5);
    			}

    			GPIOA->ODR &= ~(1 << 4);

    			Motor_Control(3);
    		}

    		delay_ms(100);

    	}

    }

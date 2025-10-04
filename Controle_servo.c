#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>
#include <string.h>

// Define as macros para os diretórios PWM
#define PWM_CHIP "/sys/class/pwm/pwmchip0"
#define PWM0 PWM_CHIP "/pwm0"

// Parâmetros do servomotor (em nanosegundos)
#define PERIODO_PWM 20000000        // 20ms = 50Hz
#define DUTY_MIN 1000000            // 1ms = 0°
#define DUTY_MAX 2000000            // 2ms = 180°
#define DUTY_MEIO 1500000           // 1.5ms = 90°

// Configurações dos LEDs
#define GPIO_CHIP "gpiochip2"       // Corresponde ao grupo GPIO C
#define LED1_PIN 0                 // Pino Led do GPIOC0
#define LED2_PIN 26                 // Pino Led do GPIOC26

// Número de passos para movimentação suave
#define NUM_PASSOS 100
#define DELAY_PASSO 20000           // 20ms entre passos

// Função que escreve em arquivos do sysfs
void writeToFile(const char *path, const char *value) {
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("Erro ao abrir arquivo");
        exit(1);
    }
    fprintf(fp, "%s", value);
    fclose(fp);
}

// Função para inicializar o PWM
void inicializarPWM() {
    printf("Inicializando PWM...\n");
    
    // Exportar PWM0
    writeToFile(PWM_CHIP "/export", "0");
    sleep(1); // Aguardar a criação dos arquivos
    
    // Configurar o período para 20ms (50Hz)
    char periodo[20];
    sprintf(periodo, "%d", PERIODO_PWM);
    writeToFile(PWM0 "/period", periodo);
    
    // Configurar duty cycle inicial (posição 0°)
    char duty[20];
    sprintf(duty, "%d", DUTY_MIN);
    writeToFile(PWM0 "/duty_cycle", duty);
    
    // Ativar PWM
    writeToFile(PWM0 "/enable", "1");
    
    printf("PWM inicializado com sucesso!\n");
}

// Função para definir o duty cycle do PWM
void setPWMDutyCycle(int duty_cycle) {
    char duty[20];
    sprintf(duty, "%d", duty_cycle);
    writeToFile(PWM0 "/duty_cycle", duty);
}

// Função para converter duty cycle em ângulo (0-180°)
int dutyParaAngulo(int duty_cycle) {
    return ((duty_cycle - DUTY_MIN) * 180) / (DUTY_MAX - DUTY_MIN);
}

// Função para desativar PWM
void desativarPWM() {
    printf("Desativando PWM...\n");
    writeToFile(PWM0 "/enable", "0");
    writeToFile(PWM_CHIP "/unexport", "0");
}

int main() {
    struct gpiod_chip *chip;
    struct gpiod_line *led1, *led2;
    int ret;
    
    printf("===== Controle de Servomotor e LEDs - Labrador =====\n\n");
    
    // ===== 1) Inicializar PWM =====
    inicializarPWM();
    
    // ===== 2) Inicializar GPIOs para os LEDs =====
    printf("Inicializando GPIOs dos LEDs...\n");
    
    // Abrir o chip GPIO
    chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) {
        perror("Erro ao abrir GPIO chip");
        desativarPWM();
        return 1;
    }
    
    // Obter as linhas GPIO dos LEDs
    led1 = gpiod_chip_get_line(chip, LED1_PIN);
    led2 = gpiod_chip_get_line(chip, LED2_PIN);
    
    if (!led1 || !led2) {
        perror("Erro ao obter linhas GPIO");
        gpiod_chip_close(chip);
        desativarPWM();
        return 1;
    }
    
    // Configurar as linhas como saída
    ret = gpiod_line_request_output(led1, "led1_control", 0);
    if (ret < 0) {
        perror("Erro ao configurar LED1 como saída");
        gpiod_chip_close(chip);
        desativarPWM();
        return 1;
    }
    
    ret = gpiod_line_request_output(led2, "led2_control", 0);
    if (ret < 0) {
        perror("Erro ao configurar LED2 como saída");
        gpiod_line_release(led1);
        gpiod_chip_close(chip);
        desativarPWM();
        return 1;
    }
    
    printf("GPIOs inicializadas com sucesso!\n\n");
    
    // ===== 3) Frequência já configurada em inicializarPWM() =====
    printf("Frequência PWM: 50Hz (período de 20ms)\n");
    printf("Iniciando controle do servomotor...\n\n");
    
    // Calcular incremento de duty cycle para movimentação suave
    int incremento = (DUTY_MAX - DUTY_MIN) / NUM_PASSOS;
    
    // Loop infinito
    while (1) {
        // ===== 4) Incrementar duty cycle: 0° -> 180° =====
        printf("Movendo de 0° para 180°...\n");
        
        for (int duty = DUTY_MIN; duty <= DUTY_MAX; duty += incremento) {
            setPWMDutyCycle(duty);
            int angulo = dutyParaAngulo(duty);
            
            // ===== 7 e 8) Controlar LEDs baseado no ângulo =====
            if (angulo <= 90) {
                gpiod_line_set_value(led1, 1);  // LED1 aceso
                gpiod_line_set_value(led2, 0);  // LED2 apagado
                printf("Ângulo: %3d° | LED1: ON  | LED2: OFF\n", angulo);
            } else {
                gpiod_line_set_value(led1, 0);  // LED1 apagado
                gpiod_line_set_value(led2, 1);  // LED2 aceso
                printf("Ângulo: %3d° | LED1: OFF | LED2: ON\n", angulo);
            }
            
            usleep(DELAY_PASSO);
        }
        
        printf("\n");
        sleep(1); // Pausa na posição 180°
        
        // ===== 5) Decrementar duty cycle: 180° -> 0° =====
        printf("Movendo de 180° para 0°...\n");
        
        for (int duty = DUTY_MAX; duty >= DUTY_MIN; duty -= incremento) {
            setPWMDutyCycle(duty);
            int angulo = dutyParaAngulo(duty);
            
            // ===== 7 e 8) Controlar LEDs baseado no ângulo =====
            if (angulo <= 90) {
                gpiod_line_set_value(led1, 1);  // LED1 aceso
                gpiod_line_set_value(led2, 0);  // LED2 apagado
                printf("Ângulo: %3d° | LED1: ON  | LED2: OFF\n", angulo);
            } else {
                gpiod_line_set_value(led1, 0);  // LED1 apagado
                gpiod_line_set_value(led2, 1);  // LED2 aceso
                printf("Ângulo: %3d° | LED1: OFF | LED2: ON\n", angulo);
            }
            
            usleep(DELAY_PASSO);
        }
        
        printf("\n");
        sleep(1); // Pausa na posição 0°
        
        // ===== 6) Loop se repete indefinidamente =====
    }
    
    // Limpeza (código inalcançável, mas boa prática)
    gpiod_line_release(led1);
    gpiod_line_release(led2);
    gpiod_chip_close(chip);
    desativarPWM();
    
    return 0;
}

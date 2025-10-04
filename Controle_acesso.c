/*
 *  Sistema de Controle de Acesso com FreeRTOS
 *  Por: Mateus Moreira da Silva
 *  Data: 26-05-2025
 *
 *  Descri��o: Sistema de controle de acesso usando sem�foros de contagem com FreeRTOS.
 *  Simula um ambiente com capacidade m�xima de pessoas, controlado por bot�es para
 *  entrada/sa�da e reset. Inclui feedback visual (LEDs, matriz LED, display OLED)
 *  e sonoro (buzzer).
 *
 *  Funcionalidades:
 *  - Controle de entrada/sa�da de pessoas via bot�es
 *  - Display OLED mostrando status atual
 *  - LEDs RGB indicando diferentes estados do sistema
 *  - Matriz de LEDs 5x5 com setas coloridas
 *  - Buzzer para feedback sonoro
 *  - Reset do sistema via bot�o do joystick
 */

// ================= INCLUDES - BLIBIOTECAS NECESS�RIAS =================
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h" 
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/bootrom.h"
#include "stdio.h"
#include "animacoes_led.pio.h"

// ================= DEFINI��ES DE HARDWARE =================
// Configura��es I2C para display OLED
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

// Pinos dos bot�es
#define BOTAO_A 5           // Bot�o para entrada de pessoas
#define BOTAO_B 6           // Bot�o para sa�da de pessoas (BOOTSEL)
#define BOTAO_JOYSTICK 22   // Bot�o do joystick para reset do sistema

// Pinos dos LEDs e matriz
#define MATRIZ_LEDS 7       // Pino de controle da matriz de LEDs 5x5
#define NUM_PIXELS 25       // N�mero total de LEDs na matriz (5x5)
#define LED_RED 13          // LED RGB vermelho
#define LED_GREEN 11        // LED RGB verde  
#define LED_AZUL 12         // LED RGB azul
#define BUZZER_PIN 21       // Pino do buzzer

// Configura��es de timing
#define DEBOUNCE_DELAY_MS 300   // Delay para debounce dos bot�es

// ================= VARI�VEIS GLOBAIS DO SISTEMA =================
// Hardware PIO para controle da matriz de LEDs
PIO pio;                    // Controlador PIO
uint sm;                    // State Machine do PIO
ssd1306_t ssd;             // Estrutura do display OLED

// Vari�veis do sistema de controle de acesso
int Num_Pessoas = 0;        // Contador atual de pessoas no ambiente
int Max_pessoas = 10;       // Capacidade m�xima do ambiente
int Desenho_seta = 0;       // �ndice do padr�o atual da matriz de LEDs

// Flags de controle de estado
bool resetar_sistema = false;      // Flag para ativar som de reset
bool Entrada_nao_permetida = false; // Flag para entrada bloqueada

// ================= SEM�FOROS E MUTEXES =================
SemaphoreHandle_t xFilaPessoaSem;   // Sem�foro de contagem para controlar n�mero de pessoas
SemaphoreHandle_t xDisplayMutex;    // Mutex para proteger acesso ao display
SemaphoreHandle_t xNumPessoasMutex; // Mutex para proteger vari�vel de contagem
SemaphoreHandle_t xResetSem;        // Sem�foro bin�rio para sinalizar reset

// ================= ENUMERA��ES E ESTRUTURAS =================
/**
 * Enumera��o para cores dos LEDs
 */
typedef enum {
    COR_VERMELHO,   // Estado: capacidade m�xima atingida
    COR_VERDE,      // Estado: funcionamento normal
    COR_AZUL,       // Estado: ambiente vazio
    COR_AMARELO,    // Estado: apenas uma vaga restante
    COR_PRETO       // Estado: LEDs apagados
} CorLED;

/**
 * Estrutura para controle de debounce dos bot�es
 */
typedef struct {
    uint32_t last_time;     // Timestamp do �ltimo pressionamento
    bool last_state;        // Estado anterior do bot�o
} DebounceState;

// ================= PADR�ES DA MATRIZ DE LEDS =================
/**
 * Matriz com padr�es visuais para a matriz de LEDs 5x5
 * Cada array representa um padr�o diferente (seta/apagado)
 */
double padroes_led[2][25] = {
    // Padr�o 0: Seta apontando para direita
    {0, 0, 1, 0, 0, 
     1, 1, 1, 1, 0, 
     1, 1, 1, 1, 1, 
     1, 1, 1, 1, 0, 
     0, 0, 1, 0, 0},
    
    // Padr�o 1: Todos LEDs apagados
    {0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0}
};

// ================= HANDLERS DE INTERRUP��O =================
/**
 * Handler de interrup��o GPIO para o bot�o de reset
 * Implementa debounce por hardware e libera sem�foro para reset
 */
void gpio_irq_handler(uint gpio, uint32_t events) {
    static uint32_t last_time = 0;
    uint32_t current_time = to_us_since_boot(get_absolute_time());
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Debouncing de 300ms (300000 microsegundos)
    if ((current_time - last_time) > 300000) {
        
        // Verifica se � o bot�o correto E se est� pressionado (LOW devido ao pull-up)
        if (gpio == BOTAO_JOYSTICK && !gpio_get(BOTAO_JOYSTICK)) {
            last_time = current_time;
            
            // Libera o sem�foro bin�rio para acordar a tarefa de reset
            xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
            
            printf("Reset solicitado via ISR\n");
        }
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ================= FUN��ES DE DEBOUNCE =================
/**
 * Fun��o de debounce para bot�es
 * Implementa debounce por software com intervalo de 250ms
 * 
 * gpio Pino GPIO do bot�o
 * state Estrutura de estado do debounce
 * return true se bot�o foi pressionado (ap�s debounce)
 */
bool Pressed_debounce(uint gpio, DebounceState *state) {
    bool pressed = !gpio_get(gpio);  // true quando pressionado (pull-up)
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Se bot�o pressionado E tempo suficiente passou desde �ltimo pressionamento
    if (pressed && (now - state->last_time) > 250) {  // 250ms entre pressionamentos
        state->last_time = now;
        return true;
    }
    
    return false;
}

// ================= FUN��ES DE CONFIGURA��O DE HARDWARE =================
/**
 * Configura um pino GPIO para sa�da PWM
 * 
 *  led_pin Pino do LED
 *  clk_div Divisor de clock
 *  wrap Valor m�ximo do contador PWM
 */
void config_PWM(uint led_pin, float clk_div, uint wrap) {
    gpio_set_function(led_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(led_pin);
    uint channel = pwm_gpio_to_channel(led_pin);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clk_div);
    pwm_config_set_wrap(&config, wrap);

    pwm_init(slice, &config, true);
    pwm_set_chan_level(slice, channel, 0);  // Inicia apagado
}

// ================= FUN��ES DE CONTROLE DA MATRIZ DE LEDS =================
/**
 * Converte valores RGBW em formato de cor para a matriz de LEDs
 * 
 * r Intensidade do vermelho (0.0 - 1.0)
 * g Intensidade do verde (0.0 - 1.0) 
 * b Intensidade do azul (0.0 - 1.0)
 * w Intensidade do branco (0.0 - 1.0)
 * return Valor de cor de 32 bits para o LED
 */
uint32_t matrix_rgb(double r, double g, double b, double w) {
    uint8_t red = (uint8_t)(r * 75);
    uint8_t green = (uint8_t)(g * 75);
    uint8_t blue = (uint8_t)(b * 75);
    uint8_t white = (uint8_t)(w * 75);
    return (green << 24) | (red << 16) | (blue << 8) | white;
}

/**
 * Atualiza toda a matriz de LEDs com o padr�o e cor especificados
 * 
 * r Intensidade do vermelho
 * g Intensidade do verde
 * b Intensidade do azul  
 * w Intensidade do branco
 */
void Desenho_matriz_leds(double r, double g, double b, double w) {
    for (int i = 0; i < NUM_PIXELS; i++) {
        double valor = padroes_led[Desenho_seta][i];
        uint32_t cor_led = (valor > 0) ? matrix_rgb(g, r, b, w) : matrix_rgb(0, 0, 0, 0);
        pio_sm_put_blocking(pio, sm, cor_led);
    }
}

/**
 * Atualiza a matriz de LEDs usando cor pr�-definida
 * 
 * cor Cor do enum CorLED
 */
void Desenho_matriz_leds_cor(CorLED cor) {
    double r = 0, g = 0, b = 0, p = 0;
    switch (cor) {
        case COR_VERMELHO: r = 1.0; break;
        case COR_VERDE: g = 1.0; break;
        case COR_AZUL: b = 1.0; break;
        case COR_AMARELO: r = 1.0; g = 1.0; break;
        case COR_PRETO: p = 1.0; break;
    }
    Desenho_matriz_leds(g, r, b, p);
}

// ================= FUN��ES DE ANIMA��O DA MATRIZ =================
/**
 * Anima��o de seta verde (estado normal - pode entrar)
 */
void Matriz_Seta_Verde(){
    Desenho_seta = 0;  // Padr�o seta
    Desenho_matriz_leds_cor(COR_VERDE);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;  // Padr�o apagado
    Desenho_matriz_leds_cor(COR_VERDE);
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * Anima��o de seta amarela (estado aten��o - �ltima vaga)
 */
void Matriz_Seta_Amarela(){
    Desenho_seta = 0;
    Desenho_matriz_leds_cor(COR_AMARELO);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;
    Desenho_matriz_leds_cor(COR_AMARELO);
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * Anima��o de seta vermelha (estado cr�tico - lotado)
 */
void Matriz_Seta_Vermelha(){
    Desenho_seta = 0;
    Desenho_matriz_leds_cor(COR_VERMELHO);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;
    Desenho_matriz_leds_cor(COR_VERMELHO);
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * Anima��o de seta azul (estado inicial - vazio)
 */
void Matriz_Seta_Azul(){
    Desenho_seta = 0;
    Desenho_matriz_leds_cor(COR_AZUL);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;  
    Desenho_matriz_leds_cor(COR_AZUL);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// ================= FUN��ES DE CONTROLE DO BUZZER =================
/**
 * Toca som no buzzer com frequ�ncia e dura��o espec�ficas
 * 
 * buzzer_pin Pino do buzzer
 * freq_hz Frequ�ncia em Hz
 * duracao_ms Dura��o em milissegundos
 */
void buzzer_tocar(uint buzzer_pin, uint freq_hz, uint duracao_ms) {
    float clk_div = 125.0f;
    uint wrap = 125000000 / (clk_div * freq_hz);
    
    config_PWM(buzzer_pin, clk_div, wrap);

    uint slice = pwm_gpio_to_slice_num(buzzer_pin);
    uint channel = pwm_gpio_to_channel(buzzer_pin);

    pwm_set_chan_level(slice, channel, wrap / 2);  // 50% duty cycle
    vTaskDelay(pdMS_TO_TICKS(duracao_ms));
    pwm_set_chan_level(slice, channel, 0);         // Desliga
}

/**
 * Sequ�ncia sonora para indicar sistema resetado
 */
void Som_Sistema_Resetado() {
    for (int i = 0; i < 2; i++) {
        buzzer_tocar(BUZZER_PIN, 100, 100);  // Tom grave
        buzzer_tocar(BUZZER_PIN, 200, 100);  // Tom agudo
        vTaskDelay(pdMS_TO_TICKS(50));       // Pausa curta
    }
}

/**
 * Som de alerta para sistema cheio
 */
void Som_Sistema_Cheio() {
    buzzer_tocar(BUZZER_PIN, 200, 200);  // Tom m�dio prolongado
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ================= TAREFAS DO FREERTOS =================
/**
 * TAREFA: Controle de Entrada
 * Monitora bot�o A para entrada de pessoas
 * Incrementa sem�foro de contagem quando pressionado
 */
void vEntradaTask(void *params)
{
    DebounceState btn_a = {0, false};

    while (true)
    {
        if (Pressed_debounce(BOTAO_A, &btn_a)) {

            // Verifica se j� atingiu capacidade m�xima
            if(Num_Pessoas == Max_pessoas){
                Entrada_nao_permetida = true;
            }

            // Incrementa sem�foro (pessoa entrando)
            if (xSemaphoreGive(xFilaPessoaSem) == pdTRUE) {
                // Atualiza contador com prote��o de mutex
                if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
                    Num_Pessoas = uxSemaphoreGetCount(xFilaPessoaSem);
                    printf("Pessoa ENTROU - Total: %d\n", Num_Pessoas);
                    xSemaphoreGive(xNumPessoasMutex);
                }
            } else {
                printf("ERRO: Fila lotada!\n");
            }        
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield para outras tasks
    }
}

/**
 * TAREFA: Controle de Sa�da  
 * Monitora bot�o B para sa�da de pessoas
 * Decrementa sem�foro de contagem quando pressionado
 */
void vSaidaTask(void *params)
{
    DebounceState btn_b = {0, false};

    while (true)
    {
        if (Pressed_debounce(BOTAO_B, &btn_b)) {

            // Decrementa sem�foro (pessoa saindo)
            if (xSemaphoreTake(xFilaPessoaSem, 0) == pdTRUE) {
                // Atualiza contador com prote��o de mutex
                if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
                    Num_Pessoas = uxSemaphoreGetCount(xFilaPessoaSem);
                    printf("Pessoa SAIU - Total: %d\n", Num_Pessoas);
                    xSemaphoreGive(xNumPessoasMutex);
                }
            } else {
                printf("ERRO: Nenhuma pessoa para sair!\n");
            }        
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield para outras tasks
    }
}

/**
 * TAREFA: Reset do Sistema
 * Aguarda sinal da interrup��o e reseta todas as vari�veis
 * Zera contador de pessoas e limpa sem�foro
 */
void vResetTask(void *params)
{
    while (true)
    {
        // AGUARDA o sem�foro bin�rio ser liberado pela interrup��o
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            
            printf("Reset solicitado! Resetando sistema...\n");
            
            // Protege acesso �s vari�veis compartilhadas
            if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
                
                // Remove todas as pessoas da fila (esvazia sem�foro de contagem)
                while (uxSemaphoreGetCount(xFilaPessoaSem) > 0) {
                    xSemaphoreTake(xFilaPessoaSem, 0);
                }
                
                // Reset das vari�veis do sistema
                Num_Pessoas = 0;
                Entrada_nao_permetida = false;
                resetar_sistema = true;  // Ativa flag para buzzer tocar
                
                xSemaphoreGive(xNumPessoasMutex);
                
                printf("Sistema resetado! Pessoas: %d\n", Num_Pessoas);
            }
        }
    }
}

/**
 * TAREFA: Controle do Display OLED
 * Atualiza informa��es no display baseado no n�mero atual de pessoas
 * Mostra status do sistema e mensagens informativas
 */
void vDisplayTask(void *params) {
    char pessoas[16];       // Buffer para string de pessoas
    const char* informacao; // Mensagem de status
    int numero_anterior = -1; // Para detectar mudan�as
    
    while (true) {
        int num_atual;
        int local; // Posi��o horizontal da mensagem
        
        // L� n�mero atual com prote��o de mutex
        if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
            num_atual = Num_Pessoas;
            xSemaphoreGive(xNumPessoasMutex);
        } else {
            num_atual = 0;
        }
        
        // S� atualiza display se o n�mero mudou
        if (num_atual != numero_anterior) {
            
            // Determina mensagem baseada no n�mero atual
            if (num_atual >= Max_pessoas) {
                informacao = "NUMERO MAXIMO";
                local = 10;
            } else if (num_atual == (Max_pessoas - 1)) {
                informacao = "APENAS 1 VAGA";
                local = 10;
            } else {
                informacao = "PODE ENTRAR";
                local = 20;
            }
            
            // Verifica se � tela de reset ou tela normal
            if(resetar_sistema){
                // Tela de confirma��o de reset
                ssd1306_fill(&ssd, 0);
                ssd1306_rect(&ssd, 1, 1, 125, 60, 1, 0);
                ssd1306_draw_string(&ssd, "SISTEMA", 25, 25);
                ssd1306_draw_string(&ssd, "RESETADO", 25, 35);
                ssd1306_send_data(&ssd);
                vTaskDelay(pdMS_TO_TICKS(1000));

            } else {
                // Tela normal de opera��o
                if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
                    ssd1306_fill(&ssd, 0);                          // Limpa display
                    ssd1306_rect(&ssd, 1, 1, 125, 60, 1, 0);        // Borda
                    ssd1306_line(&ssd, 3, 24, 125, 24, 1);          // Linha divis�ria 1
                    ssd1306_line(&ssd, 3, 43, 125, 43, 1);          // Linha divis�ria 2
                    ssd1306_draw_string(&ssd, "CONTROLE ACESSO", 5, 10); // T�tulo
                    
                    sprintf(pessoas, "USUARIOS: %d", num_atual);     // Contador
                    ssd1306_draw_string(&ssd, pessoas, 15, 30);
                    ssd1306_draw_string(&ssd, informacao, local, 50); // Status
                    
                    ssd1306_send_data(&ssd);
                    xSemaphoreGive(xDisplayMutex);
                }
            }
            
            numero_anterior = num_atual;
            printf("Display atualizado: %d pessoas - %s\n", num_atual, informacao);
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));  // Atualiza��o a cada 200ms
    }
}

/**
 * TAREFA: Controle do Buzzer
 * Monitora flags de estado e executa sequ�ncias sonoras apropriadas
 */
void vBuzzerTask(void *params)
{
    // Configura��o inicial do buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    while (true) {
        // Verifica flags e executa sons correspondentes
        if (Entrada_nao_permetida) {
            Som_Sistema_Cheio();
            Entrada_nao_permetida = false; // Reset da flag
            
        } else if(resetar_sistema) {
            Som_Sistema_Resetado();
            resetar_sistema = false; // Reset da flag
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * TAREFA: Controle da Matriz de LEDs
 * Gerencia anima��es da matriz 5x5 baseado no estado do sistema
 * Estados: Vazio(azul), Normal(verde), Aten��o(amarelo), Cheio(vermelho)
 */
void vMatriz_LedsTask(void *params)
{
    // Inicializa��o PIO para controle da matriz
    pio = pio0;
    uint offset = pio_add_program(pio, &animacoes_led_program);
    sm = pio_claim_unused_sm(pio, true);
    animacoes_led_program_init(pio, sm, offset, MATRIZ_LEDS);

    while (true)
    {
        // Controle baseado no n�mero de pessoas
        if (Num_Pessoas == 0) {
            // Estado inicial - ambiente vazio
            Matriz_Seta_Azul();

        } else if ((Num_Pessoas > 0) && (Num_Pessoas <= (Max_pessoas - 2))) {
            // Estado normal - funcionamento regular  
            Matriz_Seta_Verde();

        } else if (Num_Pessoas == (Max_pessoas - 1)) {
            // Estado de aten��o - apenas uma vaga
            Matriz_Seta_Amarela();

        } else if (Num_Pessoas == Max_pessoas){
            // Estado cr�tico - capacidade m�xima
            Matriz_Seta_Vermelha();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/**
 * TAREFA: Controle dos LEDs RGB
 * Controla LEDs individuais RGB baseado no estado do sistema
 * Cores correspondem aos mesmos estados da matriz
 */
void vLeds_RGBTask(void *params)
{
    // Configura��o inicial dos LEDs RGB
    gpio_init(LED_RED);
    gpio_init(LED_GREEN);
    gpio_init(LED_AZUL);

    // Configura��o PWM para controle de intensidade
    config_PWM(LED_GREEN, 4.0f, 100);
    config_PWM(LED_RED, 4.0f, 100);
    config_PWM(LED_AZUL, 4.0f, 100);
    
    while (true)
    {
        // Obten��o dos slices e channels PWM
        uint slice_red = pwm_gpio_to_slice_num(LED_RED);
        uint channel_red = pwm_gpio_to_channel(LED_RED);
        uint slice_green = pwm_gpio_to_slice_num(LED_GREEN);
        uint channel_green = pwm_gpio_to_channel(LED_GREEN);
        uint slice_blue = pwm_gpio_to_slice_num(LED_AZUL);
        uint channel_blue = pwm_gpio_to_channel(LED_AZUL);

        // Controle de cores baseado no n�mero de pessoas
        if (Num_Pessoas == 0) {
            // Estado vazio - LED azul
            pwm_set_chan_level(slice_green, channel_green, 0);
            pwm_set_chan_level(slice_red, channel_red, 0);
            pwm_set_chan_level(slice_blue, channel_blue, 100);

        } else if ((Num_Pessoas > 0) && (Num_Pessoas <= (Max_pessoas - 2))) {
            // Estado normal - LED verde
            pwm_set_chan_level(slice_green, channel_green, 100);
            pwm_set_chan_level(slice_red, channel_red, 0);
            pwm_set_chan_level(slice_blue, channel_blue, 0);

        } else if (Num_Pessoas == (Max_pessoas - 1)) {
            // Estado aten��o - LED amarelo (verde + vermelho)
            pwm_set_chan_level(slice_green, channel_green, 100);
            pwm_set_chan_level(slice_red, channel_red, 100);
            pwm_set_chan_level(slice_blue, channel_blue, 0);

        } else if (Num_Pessoas == Max_pessoas){
            // Estado cr�tico - LED vermelho
            pwm_set_chan_level(slice_green, channel_green, 0);
            pwm_set_chan_level(slice_red, channel_red, 100);
            pwm_set_chan_level(slice_blue, channel_blue, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ================= FUN��O PRINCIPAL =================
int main()
{
    // ===== INICIALIZA��O DO SISTEMA =====
    stdio_init_all();   // Inicializa stdio para printf
    sleep_ms(2000);     // Aguarda estabiliza��o

    // ===== CONFIGURA��O DO DISPLAY OLED =====
    i2c_init(I2C_PORT, 400 * 1000);                    // I2C a 400kHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);                              // Pull-up interno
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // // ===== CONFIGURA��O DOS BOT�ES =====
    // Bot�o A - Entrada de pessoas
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);                              // Pull-up interno

    // Bot�o B - Sa�da de pessoas (BOOTSEL)
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);                              // Pull-up interno

    // Bot�o do Joystick - Reset do sistema
    gpio_init(BOTAO_JOYSTICK);
    gpio_set_dir(BOTAO_JOYSTICK, GPIO_IN);
    gpio_pull_up(BOTAO_JOYSTICK);                       // Pull-up interno
    // Configura interrup��o para detec��o de borda de descida
    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // ===== CRIA��O DOS SEM�FOROS E MUTEXES =====
    // Mutex para proteger acesso ao display OLED
    xDisplayMutex = xSemaphoreCreateMutex();
    
    // Mutex para proteger vari�vel de contagem de pessoas
    xNumPessoasMutex = xSemaphoreCreateMutex();
    
    // Sem�foro bin�rio para sinalizar reset do sistema
    xResetSem = xSemaphoreCreateBinary();
    
    // Sem�foro de contagem para controlar n�mero de pessoas
    // M�ximo: Max_pessoas, Inicial: 0 (ambiente vazio)
    xFilaPessoaSem = xSemaphoreCreateCounting(Max_pessoas, 0);

    // ===== CRIA��O DAS TAREFAS DO FREERTOS =====
    // Tarefa para controle de entrada (Bot�o A)
    xTaskCreate(vEntradaTask, "EntradaTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para controle de sa�da (Bot�o B)  
    xTaskCreate(vSaidaTask, "SaidaTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para reset do sistema (Bot�o Joystick)
    xTaskCreate(vResetTask, "ResetTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para atualiza��o do display OLED
    xTaskCreate(vDisplayTask, "DisplayTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para controle dos LEDs RGB individuais
    xTaskCreate(vLeds_RGBTask, "Leds_RGBTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    
    // Tarefa para controle do buzzer (feedback sonoro)
    xTaskCreate(vBuzzerTask, "BuzzerTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    
    // Tarefa para controle da matriz de LEDs 5x5
    xTaskCreate(vMatriz_LedsTask, "Matriz_LedsTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);

    // ===== IN�CIO DO SCHEDULER DO FREERTOS =====
    // Inicia o agendador de tarefas - nunca retorna se bem-sucedido
    vTaskStartScheduler();
    
    // Se chegou aqui, houve erro na inicializa��o do FreeRTOS
    panic_unsupported();
}

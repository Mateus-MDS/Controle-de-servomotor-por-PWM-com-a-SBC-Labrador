/*
 *  Sistema de Controle de Acesso com FreeRTOS
 *  Por: Mateus Moreira da Silva
 *  Data: 26-05-2025
 *
 *  Descrição: Sistema de controle de acesso usando semáforos de contagem com FreeRTOS.
 *  Simula um ambiente com capacidade máxima de pessoas, controlado por botões para
 *  entrada/saída e reset. Inclui feedback visual (LEDs, matriz LED, display OLED)
 *  e sonoro (buzzer).
 *
 *  Funcionalidades:
 *  - Controle de entrada/saída de pessoas via botões
 *  - Display OLED mostrando status atual
 *  - LEDs RGB indicando diferentes estados do sistema
 *  - Matriz de LEDs 5x5 com setas coloridas
 *  - Buzzer para feedback sonoro
 *  - Reset do sistema via botão do joystick
 */

// ================= INCLUDES - BLIBIOTECAS NECESSÁRIAS =================
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

// ================= DEFINIÇÕES DE HARDWARE =================
// Configurações I2C para display OLED
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

// Pinos dos botões
#define BOTAO_A 5           // Botão para entrada de pessoas
#define BOTAO_B 6           // Botão para saída de pessoas (BOOTSEL)
#define BOTAO_JOYSTICK 22   // Botão do joystick para reset do sistema

// Pinos dos LEDs e matriz
#define MATRIZ_LEDS 7       // Pino de controle da matriz de LEDs 5x5
#define NUM_PIXELS 25       // Número total de LEDs na matriz (5x5)
#define LED_RED 13          // LED RGB vermelho
#define LED_GREEN 11        // LED RGB verde  
#define LED_AZUL 12         // LED RGB azul
#define BUZZER_PIN 21       // Pino do buzzer

// Configurações de timing
#define DEBOUNCE_DELAY_MS 300   // Delay para debounce dos botões

// ================= VARIÁVEIS GLOBAIS DO SISTEMA =================
// Hardware PIO para controle da matriz de LEDs
PIO pio;                    // Controlador PIO
uint sm;                    // State Machine do PIO
ssd1306_t ssd;             // Estrutura do display OLED

// Variáveis do sistema de controle de acesso
int Num_Pessoas = 0;        // Contador atual de pessoas no ambiente
int Max_pessoas = 10;       // Capacidade máxima do ambiente
int Desenho_seta = 0;       // Índice do padrão atual da matriz de LEDs

// Flags de controle de estado
bool resetar_sistema = false;      // Flag para ativar som de reset
bool Entrada_nao_permetida = false; // Flag para entrada bloqueada

// ================= SEMÁFOROS E MUTEXES =================
SemaphoreHandle_t xFilaPessoaSem;   // Semáforo de contagem para controlar número de pessoas
SemaphoreHandle_t xDisplayMutex;    // Mutex para proteger acesso ao display
SemaphoreHandle_t xNumPessoasMutex; // Mutex para proteger variável de contagem
SemaphoreHandle_t xResetSem;        // Semáforo binário para sinalizar reset

// ================= ENUMERAÇÕES E ESTRUTURAS =================
/**
 * Enumeração para cores dos LEDs
 */
typedef enum {
    COR_VERMELHO,   // Estado: capacidade máxima atingida
    COR_VERDE,      // Estado: funcionamento normal
    COR_AZUL,       // Estado: ambiente vazio
    COR_AMARELO,    // Estado: apenas uma vaga restante
    COR_PRETO       // Estado: LEDs apagados
} CorLED;

/**
 * Estrutura para controle de debounce dos botões
 */
typedef struct {
    uint32_t last_time;     // Timestamp do último pressionamento
    bool last_state;        // Estado anterior do botão
} DebounceState;

// ================= PADRÕES DA MATRIZ DE LEDS =================
/**
 * Matriz com padrões visuais para a matriz de LEDs 5x5
 * Cada array representa um padrão diferente (seta/apagado)
 */
double padroes_led[2][25] = {
    // Padrão 0: Seta apontando para direita
    {0, 0, 1, 0, 0, 
     1, 1, 1, 1, 0, 
     1, 1, 1, 1, 1, 
     1, 1, 1, 1, 0, 
     0, 0, 1, 0, 0},
    
    // Padrão 1: Todos LEDs apagados
    {0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0, 
     0, 0, 0, 0, 0}
};

// ================= HANDLERS DE INTERRUPÇÃO =================
/**
 * Handler de interrupção GPIO para o botão de reset
 * Implementa debounce por hardware e libera semáforo para reset
 */
void gpio_irq_handler(uint gpio, uint32_t events) {
    static uint32_t last_time = 0;
    uint32_t current_time = to_us_since_boot(get_absolute_time());
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Debouncing de 300ms (300000 microsegundos)
    if ((current_time - last_time) > 300000) {
        
        // Verifica se é o botão correto E se está pressionado (LOW devido ao pull-up)
        if (gpio == BOTAO_JOYSTICK && !gpio_get(BOTAO_JOYSTICK)) {
            last_time = current_time;
            
            // Libera o semáforo binário para acordar a tarefa de reset
            xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
            
            printf("Reset solicitado via ISR\n");
        }
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ================= FUNÇÕES DE DEBOUNCE =================
/**
 * Função de debounce para botões
 * Implementa debounce por software com intervalo de 250ms
 * 
 * gpio Pino GPIO do botão
 * state Estrutura de estado do debounce
 * return true se botão foi pressionado (após debounce)
 */
bool Pressed_debounce(uint gpio, DebounceState *state) {
    bool pressed = !gpio_get(gpio);  // true quando pressionado (pull-up)
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Se botão pressionado E tempo suficiente passou desde último pressionamento
    if (pressed && (now - state->last_time) > 250) {  // 250ms entre pressionamentos
        state->last_time = now;
        return true;
    }
    
    return false;
}

// ================= FUNÇÕES DE CONFIGURAÇÃO DE HARDWARE =================
/**
 * Configura um pino GPIO para saída PWM
 * 
 *  led_pin Pino do LED
 *  clk_div Divisor de clock
 *  wrap Valor máximo do contador PWM
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

// ================= FUNÇÕES DE CONTROLE DA MATRIZ DE LEDS =================
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
 * Atualiza toda a matriz de LEDs com o padrão e cor especificados
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
 * Atualiza a matriz de LEDs usando cor pré-definida
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

// ================= FUNÇÕES DE ANIMAÇÃO DA MATRIZ =================
/**
 * Animação de seta verde (estado normal - pode entrar)
 */
void Matriz_Seta_Verde(){
    Desenho_seta = 0;  // Padrão seta
    Desenho_matriz_leds_cor(COR_VERDE);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;  // Padrão apagado
    Desenho_matriz_leds_cor(COR_VERDE);
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * Animação de seta amarela (estado atenção - última vaga)
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
 * Animação de seta vermelha (estado crítico - lotado)
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
 * Animação de seta azul (estado inicial - vazio)
 */
void Matriz_Seta_Azul(){
    Desenho_seta = 0;
    Desenho_matriz_leds_cor(COR_AZUL);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Desenho_seta = 1;  
    Desenho_matriz_leds_cor(COR_AZUL);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// ================= FUNÇÕES DE CONTROLE DO BUZZER =================
/**
 * Toca som no buzzer com frequência e duração específicas
 * 
 * buzzer_pin Pino do buzzer
 * freq_hz Frequência em Hz
 * duracao_ms Duração em milissegundos
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
 * Sequência sonora para indicar sistema resetado
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
    buzzer_tocar(BUZZER_PIN, 200, 200);  // Tom médio prolongado
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ================= TAREFAS DO FREERTOS =================
/**
 * TAREFA: Controle de Entrada
 * Monitora botão A para entrada de pessoas
 * Incrementa semáforo de contagem quando pressionado
 */
void vEntradaTask(void *params)
{
    DebounceState btn_a = {0, false};

    while (true)
    {
        if (Pressed_debounce(BOTAO_A, &btn_a)) {

            // Verifica se já atingiu capacidade máxima
            if(Num_Pessoas == Max_pessoas){
                Entrada_nao_permetida = true;
            }

            // Incrementa semáforo (pessoa entrando)
            if (xSemaphoreGive(xFilaPessoaSem) == pdTRUE) {
                // Atualiza contador com proteção de mutex
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
 * TAREFA: Controle de Saída  
 * Monitora botão B para saída de pessoas
 * Decrementa semáforo de contagem quando pressionado
 */
void vSaidaTask(void *params)
{
    DebounceState btn_b = {0, false};

    while (true)
    {
        if (Pressed_debounce(BOTAO_B, &btn_b)) {

            // Decrementa semáforo (pessoa saindo)
            if (xSemaphoreTake(xFilaPessoaSem, 0) == pdTRUE) {
                // Atualiza contador com proteção de mutex
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
 * Aguarda sinal da interrupção e reseta todas as variáveis
 * Zera contador de pessoas e limpa semáforo
 */
void vResetTask(void *params)
{
    while (true)
    {
        // AGUARDA o semáforo binário ser liberado pela interrupção
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            
            printf("Reset solicitado! Resetando sistema...\n");
            
            // Protege acesso às variáveis compartilhadas
            if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
                
                // Remove todas as pessoas da fila (esvazia semáforo de contagem)
                while (uxSemaphoreGetCount(xFilaPessoaSem) > 0) {
                    xSemaphoreTake(xFilaPessoaSem, 0);
                }
                
                // Reset das variáveis do sistema
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
 * Atualiza informações no display baseado no número atual de pessoas
 * Mostra status do sistema e mensagens informativas
 */
void vDisplayTask(void *params) {
    char pessoas[16];       // Buffer para string de pessoas
    const char* informacao; // Mensagem de status
    int numero_anterior = -1; // Para detectar mudanças
    
    while (true) {
        int num_atual;
        int local; // Posição horizontal da mensagem
        
        // Lê número atual com proteção de mutex
        if (xSemaphoreTake(xNumPessoasMutex, portMAX_DELAY) == pdTRUE) {
            num_atual = Num_Pessoas;
            xSemaphoreGive(xNumPessoasMutex);
        } else {
            num_atual = 0;
        }
        
        // Só atualiza display se o número mudou
        if (num_atual != numero_anterior) {
            
            // Determina mensagem baseada no número atual
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
            
            // Verifica se é tela de reset ou tela normal
            if(resetar_sistema){
                // Tela de confirmação de reset
                ssd1306_fill(&ssd, 0);
                ssd1306_rect(&ssd, 1, 1, 125, 60, 1, 0);
                ssd1306_draw_string(&ssd, "SISTEMA", 25, 25);
                ssd1306_draw_string(&ssd, "RESETADO", 25, 35);
                ssd1306_send_data(&ssd);
                vTaskDelay(pdMS_TO_TICKS(1000));

            } else {
                // Tela normal de operação
                if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY) == pdTRUE) {
                    ssd1306_fill(&ssd, 0);                          // Limpa display
                    ssd1306_rect(&ssd, 1, 1, 125, 60, 1, 0);        // Borda
                    ssd1306_line(&ssd, 3, 24, 125, 24, 1);          // Linha divisória 1
                    ssd1306_line(&ssd, 3, 43, 125, 43, 1);          // Linha divisória 2
                    ssd1306_draw_string(&ssd, "CONTROLE ACESSO", 5, 10); // Título
                    
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
        
        vTaskDelay(pdMS_TO_TICKS(200));  // Atualização a cada 200ms
    }
}

/**
 * TAREFA: Controle do Buzzer
 * Monitora flags de estado e executa sequências sonoras apropriadas
 */
void vBuzzerTask(void *params)
{
    // Configuração inicial do buzzer
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
 * Gerencia animações da matriz 5x5 baseado no estado do sistema
 * Estados: Vazio(azul), Normal(verde), Atenção(amarelo), Cheio(vermelho)
 */
void vMatriz_LedsTask(void *params)
{
    // Inicialização PIO para controle da matriz
    pio = pio0;
    uint offset = pio_add_program(pio, &animacoes_led_program);
    sm = pio_claim_unused_sm(pio, true);
    animacoes_led_program_init(pio, sm, offset, MATRIZ_LEDS);

    while (true)
    {
        // Controle baseado no número de pessoas
        if (Num_Pessoas == 0) {
            // Estado inicial - ambiente vazio
            Matriz_Seta_Azul();

        } else if ((Num_Pessoas > 0) && (Num_Pessoas <= (Max_pessoas - 2))) {
            // Estado normal - funcionamento regular  
            Matriz_Seta_Verde();

        } else if (Num_Pessoas == (Max_pessoas - 1)) {
            // Estado de atenção - apenas uma vaga
            Matriz_Seta_Amarela();

        } else if (Num_Pessoas == Max_pessoas){
            // Estado crítico - capacidade máxima
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
    // Configuração inicial dos LEDs RGB
    gpio_init(LED_RED);
    gpio_init(LED_GREEN);
    gpio_init(LED_AZUL);

    // Configuração PWM para controle de intensidade
    config_PWM(LED_GREEN, 4.0f, 100);
    config_PWM(LED_RED, 4.0f, 100);
    config_PWM(LED_AZUL, 4.0f, 100);
    
    while (true)
    {
        // Obtenção dos slices e channels PWM
        uint slice_red = pwm_gpio_to_slice_num(LED_RED);
        uint channel_red = pwm_gpio_to_channel(LED_RED);
        uint slice_green = pwm_gpio_to_slice_num(LED_GREEN);
        uint channel_green = pwm_gpio_to_channel(LED_GREEN);
        uint slice_blue = pwm_gpio_to_slice_num(LED_AZUL);
        uint channel_blue = pwm_gpio_to_channel(LED_AZUL);

        // Controle de cores baseado no número de pessoas
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
            // Estado atenção - LED amarelo (verde + vermelho)
            pwm_set_chan_level(slice_green, channel_green, 100);
            pwm_set_chan_level(slice_red, channel_red, 100);
            pwm_set_chan_level(slice_blue, channel_blue, 0);

        } else if (Num_Pessoas == Max_pessoas){
            // Estado crítico - LED vermelho
            pwm_set_chan_level(slice_green, channel_green, 0);
            pwm_set_chan_level(slice_red, channel_red, 100);
            pwm_set_chan_level(slice_blue, channel_blue, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ================= FUNÇÃO PRINCIPAL =================
int main()
{
    // ===== INICIALIZAÇÃO DO SISTEMA =====
    stdio_init_all();   // Inicializa stdio para printf
    sleep_ms(2000);     // Aguarda estabilização

    // ===== CONFIGURAÇÃO DO DISPLAY OLED =====
    i2c_init(I2C_PORT, 400 * 1000);                    // I2C a 400kHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);                              // Pull-up interno
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // // ===== CONFIGURAÇÃO DOS BOTÕES =====
    // Botão A - Entrada de pessoas
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);                              // Pull-up interno

    // Botão B - Saída de pessoas (BOOTSEL)
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);                              // Pull-up interno

    // Botão do Joystick - Reset do sistema
    gpio_init(BOTAO_JOYSTICK);
    gpio_set_dir(BOTAO_JOYSTICK, GPIO_IN);
    gpio_pull_up(BOTAO_JOYSTICK);                       // Pull-up interno
    // Configura interrupção para detecção de borda de descida
    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // ===== CRIAÇÃO DOS SEMÁFOROS E MUTEXES =====
    // Mutex para proteger acesso ao display OLED
    xDisplayMutex = xSemaphoreCreateMutex();
    
    // Mutex para proteger variável de contagem de pessoas
    xNumPessoasMutex = xSemaphoreCreateMutex();
    
    // Semáforo binário para sinalizar reset do sistema
    xResetSem = xSemaphoreCreateBinary();
    
    // Semáforo de contagem para controlar número de pessoas
    // Máximo: Max_pessoas, Inicial: 0 (ambiente vazio)
    xFilaPessoaSem = xSemaphoreCreateCounting(Max_pessoas, 0);

    // ===== CRIAÇÃO DAS TAREFAS DO FREERTOS =====
    // Tarefa para controle de entrada (Botão A)
    xTaskCreate(vEntradaTask, "EntradaTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para controle de saída (Botão B)  
    xTaskCreate(vSaidaTask, "SaidaTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para reset do sistema (Botão Joystick)
    xTaskCreate(vResetTask, "ResetTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para atualização do display OLED
    xTaskCreate(vDisplayTask, "DisplayTask", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
    
    // Tarefa para controle dos LEDs RGB individuais
    xTaskCreate(vLeds_RGBTask, "Leds_RGBTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    
    // Tarefa para controle do buzzer (feedback sonoro)
    xTaskCreate(vBuzzerTask, "BuzzerTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    
    // Tarefa para controle da matriz de LEDs 5x5
    xTaskCreate(vMatriz_LedsTask, "Matriz_LedsTask", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);

    // ===== INÍCIO DO SCHEDULER DO FREERTOS =====
    // Inicia o agendador de tarefas - nunca retorna se bem-sucedido
    vTaskStartScheduler();
    
    // Se chegou aqui, houve erro na inicialização do FreeRTOS
    panic_unsupported();
}

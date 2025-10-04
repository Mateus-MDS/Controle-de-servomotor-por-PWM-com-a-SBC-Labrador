# Sistema de Controle de Servomotor e LEDs - SBC Labrador

Projeto desenvolvido para controlar um servomotor com movimentação suave (0° a 180°) e dois LEDs indicadores utilizando o SBC Labrador. O sistema utiliza PWM via sysfs para controle preciso do servo e GPIO para gerenciamento dos LEDs, oferecendo controle sincronizado baseado na posição angular do motor.

## Objetivo

Desenvolver um sistema embarcado que controla um servomotor SG90 (ou similar) com movimentação suave e progressiva, sincronizando a ativação de LEDs indicadores conforme a posição angular. O projeto demonstra o uso de PWM hardware, controle GPIO e manipulação de arquivos do sistema Linux embarcado.

## Funcionalidades

### Movimentação do Servomotor
- **Varredura Angular**: Movimento contínuo de 0° a 180° e retorno
- **Transição Suave**: 100 passos progressivos em cada direção
- **Velocidade Controlada**: 20ms de delay entre cada passo
- **Frequência PWM**: 50Hz (período de 20ms) - padrão para servomotores
- **Precisão Angular**: Controle preciso via duty cycle (1ms a 2ms)

### Sistema de Indicação por LEDs
- **LED 1 (GPIOC0)**: Aceso quando ângulo ≤ 90° (primeira metade do curso)
- **LED 2 (GPIOC26)**: Aceso quando ângulo > 90° (segunda metade do curso)
- **Feedback Visual**: Indicação clara da posição atual do servo
- **Alternância Automática**: LEDs nunca acesos simultaneamente

### Monitoramento em Tempo Real
- **Display de Console**: Impressão contínua do ângulo atual
- **Status dos LEDs**: Indicação ON/OFF para cada LED
- **Feedback Sincronizado**: Atualização a cada movimento do servo

## Componentes e Conexões Utilizados

| Componente | Interface | Pino/GPIO | Função |
|------------|-----------|-----------|--------|
| Servomotor SG90 | PWM0 | pwmchip0/pwm0 | Controle de posição angular |
| LED 1 (Vermelho) | GPIO | GPIOC0 | Indicador 0°-90° |
| LED 2 (Verde) | GPIO | GPIOC26 | Indicador 91°-180° |
| Alimentação | 5V/GND | - | Alimentação do servomotor |

### Diagrama de Conexão

```
SBC Labrador
├── PWM0 ──────────► Servomotor (Sinal)
├── GPIOC0 ────────► LED1 + Resistor 330Ω ──► GND
├── GPIOC26 ───────► LED2 + Resistor 330Ω ──► GND
└── 5V/GND ────────► Servomotor (Alimentação)
```

## Especificações Técnicas

### Parâmetros PWM
- **Período**: 20.000.000 ns (20ms) → 50Hz
- **Duty Cycle Mínimo**: 1.000.000 ns (1ms) → 0°
- **Duty Cycle Máximo**: 2.000.000 ns (2ms) → 180°
- **Duty Cycle Neutro**: 1.500.000 ns (1.5ms) → 90°

### Parâmetros de Movimento
- **Número de Passos**: 100 (ida e volta)
- **Incremento por Passo**: 10.000 ns
- **Delay entre Passos**: 20ms
- **Tempo Total (0°→180°)**: ~2 segundos
- **Tempo Total (180°→0°)**: ~2 segundos

### Configurações GPIO
- **Chip GPIO**: gpiochip2 (GPIO Grupo C)
- **Modo de Operação**: Output (saída digital)
- **Controle**: Via biblioteca libgpiod

Sistema de Controle de Servomotor e LEDs - SBC Labrador

Projeto desenvolvido para controlar um servomotor com movimentacao suave (0 a 180 graus) e dois LEDs indicadores utilizando o SBC Labrador. O sistema utiliza PWM via sysfs para controle preciso do servo e GPIO para gerenciamento dos LEDs, oferecendo controle sincronizado baseado na posicao angular do motor.

Objetivo:

Desenvolver um sistema embarcado que controla um servomotor SG90 (ou similar) com movimentacao suave e progressiva, sincronizando a ativacao de LEDs indicadores conforme a posicao angular. O projeto demonstra o uso de PWM hardware, controle GPIO e manipulacao de arquivos do sistema Linux embarcado.

Funcionalidades:

Movimentacao do Servomotor

Varredura Angular: Movimento continuo de 0 a 180 graus e retorno
Transicao Suave: 100 passos progressivos em cada direcao
Velocidade Controlada: 20ms de delay entre cada passo
Frequencia PWM: 50Hz (periodo de 20ms) - padrao para servomotores
Precisao Angular: Controle preciso via duty cycle (1ms a 2ms)

Sistema de Indicacao por LEDs:

LED 1 (GPIOC0): Aceso quando angulo <= 90 graus (primeira metade do curso)
LED 2 (GPIOC26): Aceso quando angulo > 90 graus (segunda metade do curso)
Feedback Visual: Indicacao clara da posicao atual do servo
Alternancia Automatica: LEDs nunca acesos simultaneamente

Monitoramento em Tempo Real:

Status dos LEDs: Indicacao ON/OFF para cada LED
Feedback Sincronizado: Atualizacao a cada movimento do servo

Componentes e Conexoes Utilizados:

Componente - Interface - Pino/GPIO - Funcao

Servomotor SG90 - PWM0 - pwmchip0/pwm0 - Controle de posicao angular
LED 1 (Vermelho) - GPIO - GPIOC0 - Indicador 0-90 graus
LED 2 (Verde) - GPIO - GPIOC26 - Indicador 91-180 graus
Alimentacao - 5V/GND - - -Alimentacao do servomotor

Diagrama de Conexao:

SBC Labrador
|-- PWM0 ----------> Servomotor (Sinal)
|-- GPIOC0 --------> LED1 + Resistor 330ohm --> GND
|-- GPIOC26 -------> LED2 + Resistor 330ohm --> GND
|-- 5V/GND --------> Servomotor (Alimentacao)

Especificacoes Tecnicas:

Parametros PWM

Periodo: 20.000.000 ns (20ms) = 50Hz
Duty Cycle Minimo: 1.000.000 ns (1ms) = 0 graus
Duty Cycle Maximo: 2.000.000 ns (2ms) = 180 graus
Duty Cycle Neutro: 1.500.000 ns (1.5ms) = 90 graus
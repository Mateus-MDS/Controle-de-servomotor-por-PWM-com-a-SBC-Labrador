# Sistema de Controle de Acesso com Raspberry Pi Pico

Projeto desenvolvido para simular um sistema de controle de acesso utilizando a Raspberry Pi Pico com FreeRTOS. O sistema monitora a entrada e saída de pessoas em um ambiente com capacidade limitada, fornecendo feedback visual e sonoro em tempo real sobre o status de ocupação.

## Objetivo

Desenvolver um sistema embarcado de controle de acesso que gerencia a capacidade máxima de um ambiente (10 pessoas), utilizando semáforos de contagem do FreeRTOS para sincronização entre tarefas. O projeto oferece interface intuitiva com display OLED, indicadores LED RGB, matriz de LEDs animada e feedback sonoro.

## Funcionalidades

### Estado Vazio
- **Condição**: 0 pessoas no ambiente
- Display OLED mostra "PODE ENTRAR" 
- LED RGB em **azul**
- Matriz de LEDs com seta azul piscante
- Buzzer inativo

### Estado Normal  
- **Condição**: 1 a 8 pessoas no ambiente
- Display OLED exibe contador atual e "PODE ENTRAR"
- LED RGB em **verde**
- Matriz de LEDs com seta verde piscante  
- Buzzer inativo

### Estado de Atenção
- **Condição**: 9 pessoas (apenas 1 vaga restante)
- Display OLED mostra "APENAS 1 VAGA"
- LED RGB em **amarelo** 
- Matriz de LEDs com seta amarela piscante
- Buzzer inativo

### Estado de Alerta
- **Condição**: 10 pessoas (capacidade máxima)
- Display OLED exibe "NÚMERO MÁXIMO"
- LED RGB em **vermelho**
- Matriz de LEDs com seta vermelha piscante
- Buzzer toca alerta quando tentativa de entrada é bloqueada

### Função Reset
- **Acionamento**: Botão do joystick
- Zera contador de pessoas instantaneamente
- Exibe mensagem "SISTEMA RESETADO" no display
- Buzzer toca sequência de confirmação (2 bips grave-agudo)

## Componentes e GPIOs Utilizados

| Componente | GPIO | Função |
|------------|------|--------|
| Botão A | GP5 | Entrada de pessoas |
| Botão B (BOOTSEL) | GP6 | Saída de pessoas |
| Botão Joystick | GP22 | Reset do sistema (via interrupção) |
| Display OLED SSD1306 | GP14/GP15 | Exibe status e contador |
| LED RGB (PWM) | GP11-13 | Indicação de estado por cores |
| Buzzer (PWM) | GP21 | Alertas sonoros |
| Matriz de LEDs 5x5 (PIO) | GP7 | Animações de setas coloridas |

## Multitarefa com FreeRTOS

O sistema utiliza FreeRTOS para execução concorrente e sincronizada:

### Tarefas Implementadas
- **vEntradaTask**: Monitora botão A para entrada de pessoas
- **vSaidaTask**: Monitora botão B para saída de pessoas  
- **vResetTask**: Processa reset via interrupção do joystick
- **vDisplayTask**: Atualiza informações no display OLED
- **vLeds_RGBTask**: Controla cores dos LEDs baseado no estado
- **vBuzzerTask**: Gerencia alertas sonoros
- **vMatriz_LedsTask**: Controla animações da matriz 5x5

### Sincronização
- **Semáforo de Contagem**: Controla número de pessoas (máx. 10)
- **Semáforo Binário**: Sinaliza reset entre interrupção e tarefa
- **Mutexes**: Protegem acesso ao display e variáveis compartilhadas

## Técnicas Implementadas

### Hardware
- **PWM**: Controle de brilho dos LEDs RGB e frequência do buzzer
- **PIO**: Controle customizado da matriz de LEDs WS2812B
- **Interrupções GPIO**: Detecção do botão de reset com debounce
- **I2C**: Comunicação com display OLED

### Software  
- **Debounce por Software**: Filtro de 250ms para botões
- **Debounce por Hardware**: Filtro de 300ms para interrupção
- **Semáforos de Contagem**: Implementação de fila de pessoas
- **Proteção de Concorrência**: Mutexes para recursos compartilhados
- **Máquina de Estados**: Controle visual baseado em número de pessoas

## Estados do Sistema

```
VAZIO (0) ? NORMAL (1-8) ? ATENÇÃO (9) ? ALERTA (10)
    ?           ?              ?           ?
   Azul       Verde        Amarelo     Vermelho
```

## Estrutura do Código

```
??? Configurações de Hardware
??? Variáveis Globais e Semáforos  
??? Handlers de Interrupção
??? Funções de Debounce
??? Controle PWM e PIO
??? Animações da Matriz de LEDs
??? Controle do Buzzer
??? 7 Tarefas FreeRTOS
??? Função Principal (main)
```

## Como Usar

1. **Entrada**: Pressione o Botão A para adicionar uma pessoa
2. **Saída**: Pressione o Botão B para remover uma pessoa  
3. **Reset**: Pressione o botão do joystick para zerar o sistema
4. **Monitoramento**: Observe o display OLED e LEDs para status atual

## Autor

**Mateus Moreira da Silva**  
Data: 26-05-2025

GitHub: https://github.com/Mateus-MDS/Controle-de-Acesso.git

/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 * @file           : main.c
 * @brief          : STM32F446RCTx Robot Motor Controller
 * @version        : 1.0.0
 * @author         : Pakprom Naennoi (Peace) , Electronics Engineer (Embedded Systems)
 * @date           : 2026-11-14
 *
 * @brief          : Main control logic for the robot motor controller
 * @note           : This firmware is designed for the MUT Robot Open House 2026 (18-Nov-2026) event.
 * @details
 * - DRV8833 Dual DC Motor Control
 * - TIM3 / TIM4 PWM
 * - USART1 ESP32 Communication
 * - USART2 Debug
 * - UART CRC-8 Protocol
 * - Motor Failsafe
 * - LED Status Indicators
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define Firmware_Version "1.0.0"

#define PROTOCOL_HEADER 0xA5U
#define PROTOCOL_SIZE 8U

#define CMD_SET_MOTOR 0x01U
#define CMD_STOP 0x02U
#define CMD_HEARTBEAT 0x03U
#define CMD_SET_MOTOR_XY 0x04U /* signed values encoded as value + 100 */

#define MOTOR_COAST 0U
#define MOTOR_FORWARD 1U
#define MOTOR_REVERSE 2U

#define MOTOR_TIMEOUT_MS 500U
#define UART_FRAME_GAP_MS 20U
#define UART_DEBUG_STATS 0U
#define JOYSTICK_DEBUG_MS 250U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

typedef struct
{
  uint8_t speed;
  uint8_t direction;
} MotorState_t;

static MotorState_t motor1 = {0};
static MotorState_t motor2 = {0};

/* USART1 polling RX: deliberately no HAL_UART_Receive_IT(). */
static uint8_t rx_buffer[PROTOCOL_SIZE] = {0};
static uint8_t rx_index = 0U;
static uint32_t rx_last_byte_tick = 0U;
static uint32_t last_motor_command_tick = 0U;
static uint8_t motor_command_active = 0U;
static uint8_t stop_log_pending = 0U;
static uint8_t failsafe_log_pending = 0U;
static uint32_t last_led_tick = 0U;
static uint32_t last_stats_tick = 0U;
static uint32_t rx_byte_count = 0U;
static uint32_t rx_valid_count = 0U;
static uint32_t rx_crc_errors = 0U;
static uint32_t rx_uart_errors = 0U;
static uint8_t rx_led_active = 0U;
static int16_t joy_x = 0;
static int16_t joy_y = 0;
static int16_t motor_left = 0;
static int16_t motor_right = 0;
static uint8_t joystick_debug_pending = 0U;
static uint32_t last_joystick_debug_tick = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static uint8_t CRC8_Calculate(const uint8_t *data, uint8_t length);

static void Motor_Set(uint8_t motor_id, uint8_t speed, uint8_t direction);

static void Motor_StopAll(void);
static void LED_Update(void);

static void Protocol_Parse(const uint8_t *frame);
static void Protocol_ReceiveByte(uint8_t data);
static void UART1_Poll(void);

static void Motor_Init(void);
static void Motor_Process(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ================= CRC-8 ================= */

static uint8_t CRC8_Calculate(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0;

  for (uint8_t i = 0; i < length; i++)
  {
    crc ^= data[i];

    for (uint8_t j = 0; j < 8; j++)
    {
      if (crc & 0x80U)
        crc = (uint8_t)((crc << 1) ^ 0x07U);
      else
        crc <<= 1;
    }
  }

  return crc;
}

/* ================= MOTOR ================= */

static void Motor_Set(uint8_t motor_id, uint8_t speed, uint8_t direction)
{
  TIM_HandleTypeDef *htim;
  uint32_t ch_a;
  uint32_t ch_b;
  MotorState_t *motor;

  if (motor_id == 1U)
  {
    htim = &htim3;
    ch_a = TIM_CHANNEL_1;
    ch_b = TIM_CHANNEL_2;
    motor = &motor1;
  }

  else if (motor_id == 2U)
  {
    htim = &htim4;
    ch_a = TIM_CHANNEL_1;
    ch_b = TIM_CHANNEL_2;
    motor = &motor2;
  }

  else
  {
    return;
  }

  if (speed > 100U)
    speed = 100U;

  /* ARR from CubeMX */
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);

  /* Full scale includes ARR + 1 */
  uint32_t pwm = ((uint32_t)speed * (arr + 1U)) / 100U;

  uint32_t pwm_a = 0;
  uint32_t pwm_b = 0;

  if (direction == MOTOR_FORWARD && speed > 0U)
  {
    pwm_a = pwm;
  }

  else if (direction == MOTOR_REVERSE && speed > 0U)
  {
    pwm_b = pwm;
  }

  else
  {
    speed = 0;
    direction = MOTOR_COAST;
  }

  __HAL_TIM_SET_COMPARE(htim, ch_a, pwm_a);
  __HAL_TIM_SET_COMPARE(htim, ch_b, pwm_b);

  motor->speed = speed;
  motor->direction = direction;
}

static void Motor_StopAll(void)
{
  Motor_Set(1, 0, MOTOR_COAST);
  Motor_Set(2, 0, MOTOR_COAST);
}

/* ================= LED STATUS ================= */

static void LED_Update(void)
{
  uint32_t now = HAL_GetTick();

  /* LED1: System Ready */
  HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, GPIO_PIN_SET);

  /* LED2: UART Receive Indicator */
  if (rx_led_active && (now - last_led_tick >= 50U))
  {
    rx_led_active = 0;
  }

  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, rx_led_active ? GPIO_PIN_SET : GPIO_PIN_RESET);

  /* LED3: Motor 1 */
  HAL_GPIO_WritePin(LED_3_GPIO_Port, LED_3_Pin, motor1.speed > 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);

  /* LED4: Motor 2 */
  HAL_GPIO_WritePin(LED_4_GPIO_Port, LED_4_Pin, motor2.speed > 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ================= PROTOCOL PARSER ================= */

static void Protocol_Parse(const uint8_t *frame)
{
  const uint8_t cmd = frame[1];
  const uint32_t now = HAL_GetTick();

  switch (cmd)
  {
  case CMD_SET_MOTOR:
  {
    const uint8_t s1 = frame[2];
    const uint8_t d1 = frame[3];
    const uint8_t s2 = frame[4];
    const uint8_t d2 = frame[5];

    if (frame[6] != 0U || s1 > 100U || s2 > 100U ||
        d1 > MOTOR_REVERSE || d2 > MOTOR_REVERSE)
    {
      printf("[ERROR] Invalid motor data\r\n");
      return;
    }

    Motor_Set(1U, s1, d1);
    Motor_Set(2U, s2, d2);

    motor_left = d1 == MOTOR_REVERSE ? -(int16_t)s1 : d1 == MOTOR_FORWARD ? (int16_t)s1
                                                                          : 0;

    motor_right = d2 == MOTOR_REVERSE ? -(int16_t)s2 : d2 == MOTOR_FORWARD ? (int16_t)s2
                                                                           : 0;

    if (motor1.speed == 0U && motor2.speed == 0U)
    {
      motor_command_active = 0U;
    }
    else
    {
      motor_command_active = 1U;
    }

    last_motor_command_tick = now;

    stop_log_pending = 0U;
    failsafe_log_pending = 0U;

    /* Enable Motor Debug */
    joystick_debug_pending = 2U;

    break;
  }

  case CMD_STOP:
  {
    if (motor_command_active || motor1.speed || motor2.speed)
    {
      stop_log_pending = 1U;
    }

    Motor_StopAll();

    motor_command_active = 0U;
    motor_left = 0;
    motor_right = 0;

    joystick_debug_pending = 0U;

    break;
  }

  case CMD_HEARTBEAT:
  {
    /* Heartbeat does not reset Motor Timeout */
    break;
  }

  default:
  {
    printf("[ERROR] Unknown CMD: 0x%02X\r\n", cmd);
    return;
  }
  }

  rx_led_active = 1U;
  last_led_tick = now;
}

/* ================= UART RX (POLLING) ================= */

/*
 * This project already received 0x55 successfully with HAL_UART_Receive().
 * Keep RX in polling mode until USART1 IRQ/NVIC is independently verified.
 * ESP32 sends one 8-byte frame approximately every 100 ms.
 */
static void Protocol_ReceiveByte(uint8_t data)
{
  /* Avoid combining bytes from separate damaged/incomplete frames. */
  if (rx_index != 0U && (uint32_t)(HAL_GetTick() - rx_last_byte_tick) > UART_FRAME_GAP_MS)
  {
    rx_index = 0U;
  }
  rx_last_byte_tick = HAL_GetTick();

  if (rx_index == 0U && data != PROTOCOL_HEADER)
    return;

  rx_buffer[rx_index++] = data;

  if (rx_index == PROTOCOL_SIZE)
  {
    uint8_t crc = CRC8_Calculate(rx_buffer, PROTOCOL_SIZE - 1U);

    if (crc == rx_buffer[PROTOCOL_SIZE - 1U])
    {
      rx_valid_count++;
      Protocol_Parse(rx_buffer);
    }
    else
    {
      rx_crc_errors++;
    }

    /* Resync: keep bytes beginning at the next candidate header. */
    uint8_t next_header = PROTOCOL_SIZE;
    for (uint8_t i = 1U; i < PROTOCOL_SIZE; i++)
    {
      if (rx_buffer[i] == PROTOCOL_HEADER)
      {
        next_header = i;
        break;
      }
    }
    if (crc == rx_buffer[PROTOCOL_SIZE - 1U] ||
        next_header == PROTOCOL_SIZE)
    {
      rx_index = 0U;
    }
    else
    {
      rx_index = (uint8_t)(PROTOCOL_SIZE - next_header);
      memmove(rx_buffer, &rx_buffer[next_header], rx_index);
    }
  }
}

static void UART1_Poll(void)
{
  uint8_t byte;

  /* Read several queued bytes without waiting for a new byte. */
  for (uint8_t i = 0U; i < 32U; i++)
  {
    HAL_StatusTypeDef status = HAL_UART_Receive(&huart1, &byte, 1U, 0U);

    if (status == HAL_OK)
    {
      rx_byte_count++;
      Protocol_ReceiveByte(byte);
    }
    else if (status == HAL_TIMEOUT)
    {
      break; /* No more bytes currently available. */
    }
    else
    {
      rx_uart_errors++;
      rx_index = 0U;
      /* Recover from overrun/noise errors; no active IT reception. */
      __HAL_UART_CLEAR_OREFLAG(&huart1);
      __HAL_UART_CLEAR_FEFLAG(&huart1);
      __HAL_UART_CLEAR_NEFLAG(&huart1);
      break;
    }
  }
}

/* ================= INITIALIZATION ================= */

static void Motor_Init(void)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0U);

  if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1) != HAL_OK ||
      HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2) != HAL_OK)
  {
    printf("[MOTOR] PWM start failed\r\n");
    Error_Handler();
  }

  Motor_StopAll();
  last_motor_command_tick = HAL_GetTick();
  last_stats_tick = HAL_GetTick();

  printf("\r\n============================\r\n");
  printf(" STM32 MOTOR CONTROLLER\r\n");
  printf(" SYSCLK: %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
  printf(" DRIVER: DRV8833\r\n");
  printf(" UART1: ESP32 RX (POLLING)\r\n");
  printf(" UART2: DEBUG\r\n");
  printf("============================\r\n");
  printf("[SYSTEM] READY\r\n");
}

/* ================= MAIN PROCESS ================= */

static void Motor_Process(void)
{
  /* USART1 Polling */
  UART1_Poll();

  uint32_t now = HAL_GetTick();

  /* Motor Command Timeout */
  if (motor_command_active &&
      (uint32_t)(now - last_motor_command_tick) >= MOTOR_TIMEOUT_MS)
  {
    Motor_StopAll();

    motor_command_active = 0U;
    failsafe_log_pending = 1U;
    stop_log_pending = 0U;
    joystick_debug_pending = 0U;
  }

  LED_Update();

  /* Motor / Stop / Failsafe Debug */
  if (failsafe_log_pending)
  {
    failsafe_log_pending = 0U;

    printf("[FAILSAFE] MOTOR COMMAND TIMEOUT - STOP\r\n");
  }
  else if (stop_log_pending)
  {
    stop_log_pending = 0U;

    printf("[MOTOR] STOP ALL (UART CMD)\r\n");
  }

  /* Motor Speed and Direction Debug */
  if (joystick_debug_pending == 2U &&
      (uint32_t)(HAL_GetTick() - last_joystick_debug_tick) >= JOYSTICK_DEBUG_MS)
  {
    last_joystick_debug_tick = HAL_GetTick();
    joystick_debug_pending = 0U;

    printf("[MOTOR] M1:%d%% DIR:%u | M2:%d%% DIR:%u\r\n",
           (int)motor_left,
           (unsigned)motor1.direction,
           (int)motor_right,
           (unsigned)motor2.direction);
  }

#if UART_DEBUG_STATS
  if ((uint32_t)(now - last_stats_tick) >= 2000U)
  {
    last_stats_tick = now;

    printf("[UART1] bytes=%lu valid=%lu crc_err=%lu uart_err=%lu\r\n",
           (unsigned long)rx_byte_count,
           (unsigned long)rx_valid_count,
           (unsigned long)rx_crc_errors,
           (unsigned long)rx_uart_errors);
  }
#endif
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  printf("[SYSTEM] START MUT ROBOT CONTROLLER\r\n");
  printf("[SYSTEM] FIRMWARE VERSION: %s\r\n", Firmware_Version);
  printf("[INFO] Build Date: %s %s\r\n", __DATE__, __TIME__);
  
  Motor_Init();
  printf("[SYSTEM] INITIALIZED\r\n");
  for (int i = 0; i < 5; i++)
  {
    HAL_GPIO_TogglePin(LED_1_GPIO_Port, LED_1_Pin);
    HAL_GPIO_TogglePin(LED_2_GPIO_Port, LED_2_Pin);
    HAL_GPIO_TogglePin(LED_3_GPIO_Port, LED_3_Pin);
    HAL_GPIO_TogglePin(LED_4_GPIO_Port, LED_4_Pin);
    HAL_Delay(200);
  }
  printf("[SYSTEM] COMPLETE\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Motor_Process();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 120;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ================= printf retarget ================= */
#ifdef __GNUC__
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
#endif

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

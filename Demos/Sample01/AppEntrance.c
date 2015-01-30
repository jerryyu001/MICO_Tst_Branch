/***
 * File: AppEntrace.c
 * a Sample Application.
 * 
 * */

#include "MicoPlatform.h"
#include "MICODefine.h"
#include "MICOAppDefine.h"

#define USART_BaudRate          115200
#define WAKEUP_BY_UART          1
#define LED_TRIGGER_INTERVAL    700
#define UART_RECV_TIMEOUT                   500

#define app_log(M, ...) custom_log("APP", M, ##__VA_ARGS__)
#define app_log_trace() custom_log_trace("APP")

volatile ring_buffer_t  rx_buffer;
volatile uint8_t        rx_data[UART_BUFFER_LENGTH];

static mico_timer_t _Led_TST_timer;

static void _led_TST_Timeout_handler( void* arg )
{
    (void)arg;
    MicoGpioOutputTrigger( (mico_gpio_t)MICO_SYS_LED );
}

void LedFlash()
{
  app_log_trace();
    /*Led trigger*/
  mico_init_timer(&_Led_TST_timer, LED_TRIGGER_INTERVAL, _led_TST_Timeout_handler, NULL);
  mico_start_timer(&_Led_TST_timer);
  return;
}

void LedOff(bool success)
{
  app_log_trace();
  mico_stop_timer(&_Led_TST_timer);
  mico_deinit_timer( &_Led_TST_timer );
  if (success){
      MicoGpioOutputHigh( (mico_gpio_t)MICO_RF_LED );
      MicoGpioOutputLow( (mico_gpio_t)MICO_SYS_LED );
  } else {
      MicoGpioOutputHigh( (mico_gpio_t)MICO_SYS_LED );
      MicoGpioOutputLow( (mico_gpio_t)MICO_RF_LED );
  }
  return;
}

size_t _uart_get_one_packet(uint8_t* inBuf, int inBufLen)
{
  app_log_trace();
  int datalen;
  
  while(1) {
    if( MicoUartRecv( UART_FOR_APP, inBuf, inBufLen, UART_RECV_TIMEOUT) == kNoErr){
      return inBufLen;
    }
   else{
     datalen = MicoUartGetLengthInBuffer( UART_FOR_APP );
     if(datalen){
       MicoUartRecv(UART_FOR_APP, inBuf, datalen, UART_RECV_TIMEOUT);
       return datalen;
     }
   }
  }
}
  
void uartRecv_thread(void *inContext)
{
  app_log_trace();
  mico_Context_t *Context = inContext;
  int recvlen;
  uint8_t *inDataBuffer;
  
  inDataBuffer = malloc(UART_ONE_PACKAGE_LENGTH);
  require(inDataBuffer, exit);
  
  while(1) {
      printf(".");
    recvlen = _uart_get_one_packet(inDataBuffer, UART_ONE_PACKAGE_LENGTH);
    if (recvlen <= 0)
      continue;
      printf(".");
    app_log("%s",*inDataBuffer);
  }
  
exit:
  if(inDataBuffer) free(inDataBuffer);
}

OSStatus MICOStartApplication( mico_Context_t * const inContext ){
  app_log_trace();
  OSStatus err = kNoErr;
  mico_uart_config_t uart_config;
 
 
  uart_config.baud_rate    = USART_BaudRate;
  uart_config.data_width   = DATA_WIDTH_8BIT;
  uart_config.parity       = NO_PARITY;
  uart_config.stop_bits    = STOP_BITS_1;
  uart_config.flow_control = FLOW_CONTROL_DISABLED;
  #if WAKEUP_BY_UART  
  uart_config.flags = UART_WAKEUP_ENABLE;
  #else 
  uart_config.flags = UART_WAKEUP_DISABLE;
  #endif 

  LedFlash();
  ring_buffer_init  ( (ring_buffer_t *)&rx_buffer, (uint8_t *)rx_data, UART_BUFFER_LENGTH );
  MicoUartInitialize( UART_FOR_APP, &uart_config, (ring_buffer_t *)&rx_buffer );
  err = mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "UART Recv", uartRecv_thread, STACK_SIZE_UART_RECV_THREAD, (void*)inContext );
  require_noerr_action( err, exit, app_log("ERROR: Unable to start the uart recv thread.") );
  
exit:
    return err; 
 }




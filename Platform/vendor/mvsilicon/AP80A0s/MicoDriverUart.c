/**
******************************************************************************
* @file    MicoDriverUart.c 
* @author  William Xu
* @version V1.0.0
* @date    05-May-2014
* @brief   This file provide UART driver functions.
******************************************************************************
*
*  The MIT License
*  Copyright (c) 2014 MXCHIP Inc.
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy 
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights 
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is furnished
*  to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
*  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************
*/ 


#include "MICORTOS.h"
#include "MICOPlatform.h"
#include "mico_driver_common.h"

#include "PlatformLogging.h"

/******************************************************
*                    Structures
******************************************************/

typedef struct
{
  uint32_t            rx_size;
  ring_buffer_t*      rx_buffer;
#ifndef NO_MICO_RTOS
  mico_semaphore_t    rx_complete;
  mico_semaphore_t    tx_complete;
#else
  volatile bool       rx_complete;
  volatile bool       tx_complete;
#endif
  mico_semaphore_t    sem_wakeup;
#ifdef MICO_USART_DMA_EN
  OSStatus            tx_dma_result;
  OSStatus            rx_dma_result;
#endif
} uart_interface_t;

/******************************************************
*                 Type Definitions
******************************************************/
#define RING_BUFF_ON 0
/******************************************************
*                    Constants
******************************************************/
const platform_uart_mapping_t uart_mapping[] =
{
  [MICO_UART_1] =
  {
    .usart              = Fuart,
    .rxPin              = FUART_RX_PORT,      //0, 1, 2, 0xFF, detail pls see gpio.h
    .txPin              = FUART_TX_PORT,      //0, 1, 2, 0xFF, detail pls see gpio.h
    .ctsPin             = FUART_CTS_PORT,     //0, 1, 2, 0xFF, detail pls see gpio.h
    .rtsPin             = FUART_RTS_PORT,     //0, 1, 2, 0xFF, detail pls see gpio.h
    .usart_irq_en       = FUART_INT_EN,  //FuartInterrupt [3] or BuarInterrupt [4]
    .exFifoEn           = FUART_EXFIFO_EN, 
#ifdef MICO_USART_DMA_EN
    .dmaCH              = FUART_DMA_CH,
#endif
  },
  [MICO_UART_2] = {
    .usart              = Buart,
    .rxPin              = BUART_RX_PORT,      //0, 1, 2, 3, 0xFF, detail pls see gpio.h
    .txPin              = BUART_TX_PORT,      //0, 1, 2, 3, 0xFF, detail pls see gpio.h
    .ctsPin             = BUART_CTS_PORT,     //0, 1, 2, 0xFF, detail pls see gpio.h
    .rtsPin             = BUART_RTS_PORT,     //0, 1, 2, 0xFF, detail pls see gpio.h
    .usart_irq_en       = BUART_INT_EN,  //FuartInterrupt [3] or BuarInterrupt [4]
    .exFifoEn           = BUART_EXFIFO_EN, 
#ifdef MICO_USART_DMA_EN
    .dmaCH              = BUART_DMA_CH,
#endif
  },
}; 
/******************************************************
*                   Enumerations
******************************************************/


/******************************************************
*               Variables Definitions
******************************************************/

static uart_interface_t uart_interfaces[NUMBER_OF_UART_INTERFACES];



#ifndef NO_MICO_RTOS
static mico_uart_t current_uart;
#endif

/******************************************************
*               Function Declarations
******************************************************/

static OSStatus internal_uart_init ( mico_uart_t uart, const mico_uart_config_t* config, ring_buffer_t* optional_rx_buffer );
static OSStatus platform_uart_receive_bytes( mico_uart_t uart, void* data, uint32_t size, uint32_t timeout );


/* Interrupt service functions - called from interrupt vector table */
#ifndef NO_MICO_RTOS
static void thread_wakeup(void *arg);
static void RX_PIN_WAKEUP_handler(void *arg);
#endif


/******************************************************
*               Function Definitions
******************************************************/
OSStatus MicoUartInitialize( mico_uart_t uart, const mico_uart_config_t* config, ring_buffer_t* optional_rx_buffer )
{
  return internal_uart_init(uart, config, optional_rx_buffer);
  
}

/** Only for STDIO_UART, which define in BoardConfig/(Board)/platfom.h */
OSStatus MicoStdioUartInitialize( const mico_uart_config_t* config, ring_buffer_t* optional_rx_buffer )
{
  return internal_uart_init(STDIO_UART, config, optional_rx_buffer);
}

//mico_uart_t defined in platform.h (BoardConfig/) 
OSStatus internal_uart_init( mico_uart_t uart, const mico_uart_config_t* config, ring_buffer_t* optional_rx_buffer )
{
#ifndef NO_MICO_RTOS
  mico_rtos_init_semaphore(&uart_interfaces[uart].tx_complete, 1);
  mico_rtos_init_semaphore(&uart_interfaces[uart].rx_complete, 1);
#else
  uart_interfaces[uart].tx_complete = false;
  uart_interfaces[uart].rx_complete = false;
#endif  
  MicoMcuPowerSaveConfig(false);  
    /* Configure the UART TX/RX pins */
  GpioFuartRxIoConfig(1); //1.b[6] 0xFF disable rx.
  GpioFuartTxIoConfig(1);//1.b[7], FUART_TX_PORT

#ifndef NO_MICO_RTOS
  if(config->flags & UART_WAKEUP_ENABLE){
    current_uart = uart;
    mico_rtos_init_semaphore( &uart_interfaces[uart].sem_wakeup, 1 );
    mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "UART_WAKEUP", thread_wakeup, 0x100, &current_uart);
  }
#endif 

  FuartInit(115200, 8, 0, 1);//TBD

#if RING_BUFF_ON 
  if (optional_rx_buffer != NULL)
  {
     //  Note that the ring_buffer should've been initialised first
    uart_interfaces[uart].rx_buffer = optional_rx_buffer;
    uart_interfaces[uart].rx_size   = 0;
    platform_uart_receive_bytes( uart, optional_rx_buffer->buffer, optional_rx_buffer->size, 0 );
  }  
#endif
  MicoMcuPowerSaveConfig(true);  
  return kNoErr;
}

OSStatus MicoUartFinalize( mico_uart_t uart )
{
  
#ifndef NO_MICO_RTOS
  mico_rtos_deinit_semaphore(&uart_interfaces[uart].rx_complete);
  mico_rtos_deinit_semaphore(&uart_interfaces[uart].tx_complete);
#endif  
  
  return kNoErr;
}

OSStatus MicoUartSend( mico_uart_t uart, const void* data, uint32_t size )
{
    uint32_t ret;
    //  /* Reset DMA transmission result. The result is assigned in interrupt handler */
  // uart_interfaces[uart].tx_dma_result = kGeneralErr;
  
  MicoMcuPowerSaveConfig(false);  
   
  if (uart == MICO_UART_1)
    ret = FuartSend((uint8_t *)data, size); 
  else 
    ret = BuartSend((uint8_t *)data, size);
  if (ret > 0){
        #ifndef NO_MICO_RTOS
            mico_rtos_set_semaphore( &uart_interfaces[ uart ].tx_complete );
        #else
            uart_interfaces[ uart ].tx_complete = true;
        #endif
   }
#ifndef NO_MICO_RTOS
  mico_rtos_get_semaphore( &uart_interfaces[ uart ].tx_complete, MICO_NEVER_TIMEOUT );
#else 
  while(uart_interfaces[ uart ].tx_complete == false);
  uart_interfaces[ uart ].tx_complete = false;
#endif
//  return uart_interfaces[uart].tx_dma_result; 
  MicoMcuPowerSaveConfig(true);

  return kNoErr;
}

OSStatus MicoUartRecv( mico_uart_t uart, void* data, uint32_t size, uint32_t timeout )
{
    uart = MICO_UART_1; //test
#if RING_BUFF_ON
  if (uart_interfaces[uart].rx_buffer != NULL)
  {
    while (size != 0)
    {
      uint32_t transfer_size = MIN(uart_interfaces[uart].rx_buffer->size / 2, size);
      
      /* Check if ring buffer already contains the required amount of data. */
      if ( transfer_size > ring_buffer_used_space( uart_interfaces[uart].rx_buffer ) )
      {
        /* Set rx_size and wait in rx_complete semaphore until data reaches rx_size or timeout occurs */
        uart_interfaces[uart].rx_size = transfer_size;
#ifndef NO_MICO_RTOS
        if ( mico_rtos_get_semaphore( &uart_interfaces[uart].rx_complete, timeout) != kNoErr )
        {
          uart_interfaces[uart].rx_size = 0;
          return kTimeoutErr;
        }
#else
        uart_interfaces[uart].rx_complete = false;
        int delay_start = mico_get_time_no_os();
        while(uart_interfaces[uart].rx_complete == false){
          if(mico_get_time_no_os() >= delay_start + timeout && timeout != MICO_NEVER_TIMEOUT){
            uart_interfaces[uart].rx_size = 0;
            return kTimeoutErr;
          }
        }
#endif
        /* Reset rx_size to prevent semaphore being set while nothing waits for the data */
        uart_interfaces[uart].rx_size = 0;
      }
     
      size -= transfer_size;
      
      // Grab data from the buffer
      do
      {
        uint8_t* available_data;
        uint32_t bytes_available;
         //platform_log("uart receive 03"); 
        ring_buffer_get_data( uart_interfaces[uart].rx_buffer, &available_data, &bytes_available );
        bytes_available = MIN( bytes_available, transfer_size );
        memcpy( data, available_data, bytes_available );
        transfer_size -= bytes_available;
        data = ( (uint8_t*) data + bytes_available );
        ring_buffer_consume( uart_interfaces[uart].rx_buffer, bytes_available );
      } while ( transfer_size != 0 );
    }
    
    if ( size != 0 )
    {
      return kGeneralErr;
    }
    else
    {
      return kNoErr;
    }
  }
  else
  {
    return platform_uart_receive_bytes( uart, data, size, timeout );
  }
#else
    return platform_uart_receive_bytes( uart, data, size, timeout );
#endif 
 // return kNoErr;
}


static OSStatus platform_uart_receive_bytes( mico_uart_t uart, void* data, uint32_t size, uint32_t timeout )
{
    uint32_t retVal = 0;
  /* Reset DMA transmission result. The result is assigned in interrupt handler */
 //  uart_interfaces[uart].rx_dma_result = kGeneralErr;
   if ( uart == MICO_UART_1 ){
    retVal = FuartRecv(data, size, timeout); //TBD!
   } else {
    retVal = BuartRecv(data, size, timeout); 
   }
   
   if(retVal > 0) { 
      #ifndef NO_MICO_RTOS
         mico_rtos_set_semaphore( &uart_interfaces[uart].rx_complete );
      #else
         uart_interfaces[uart ].rx_complete = true;
      #endif
      return kNoErr;
    }
  if ( timeout > 0 )
   {
#ifndef NO_MICO_RTOS
    mico_rtos_get_semaphore( &uart_interfaces[uart].rx_complete, timeout );
#else
    uart_interfaces[uart].rx_complete = false;
    int delay_start = mico_get_time_no_os();
    while(uart_interfaces[uart].rx_complete == false){
      if(mico_get_time_no_os() >= delay_start + timeout && timeout != MICO_NEVER_TIMEOUT){
        break;
      }
    }    
#endif
    return kGeneralErr; //uart_interfaces[uart].rx_dma_result;
  }   
  return kGeneralErr;              // kNoErr;
}


uint32_t MicoUartGetLengthInBuffer( mico_uart_t uart )
{
#if RING_BUFF_ON
  return ring_buffer_used_space( uart_interfaces[uart].rx_buffer );
#else
  return 1; //test
#endif
}

#ifndef NO_MICO_RTOS
static void thread_wakeup(void *arg)
{
  mico_uart_t uart = *(mico_uart_t *)arg;
    uart = MICO_UART_1; //test
  while(1){
     if(mico_rtos_get_semaphore(&uart_interfaces[ uart ].sem_wakeup, 1000) != kNoErr){
//      gpio_irq_enable(uart_mapping[1].pin_rx->bank, uart_mapping[1].pin_rx->number, IRQ_TRIGGER_FALLING_EDGE, RX_PIN_WAKEUP_handler, &uart);
      MicoMcuPowerSaveConfig(true);
    }   
  }
}
#endif

/******************************************************
*            Interrupt Service Routines
******************************************************/
#ifndef NO_MICO_RTOS
void RX_PIN_WAKEUP_handler(void *arg)
{
  (void)arg;
  mico_uart_t uart = *(mico_uart_t *)arg;
  
//  RCC_AHB1PeriphClockCmd(uart_mapping[ uart ].pin_rx->peripheral_clock, ENABLE);
//  uart_mapping[ uart ].usart_peripheral_clock_func ( uart_mapping[uart].usart_peripheral_clock,  ENABLE );
//  uart_mapping[uart].rx_dma_peripheral_clock_func  ( uart_mapping[uart].rx_dma_peripheral_clock, ENABLE );
//  
//  gpio_irq_disable(uart_mapping[uart].pin_rx->bank, uart_mapping[uart].pin_rx->number); 
  
  MicoMcuPowerSaveConfig(false);
  mico_rtos_set_semaphore(&uart_interfaces[uart].sem_wakeup);
}
#endif

// end file







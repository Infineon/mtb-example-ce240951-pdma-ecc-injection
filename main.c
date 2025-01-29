/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the P-DMA ECC Injection Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cy_sysfault.h"
#include "cy_sysint.h"
#include "cy_retarget_io.h"
#include <inttypes.h>

/*******************************************************************************
* Macros
*******************************************************************************/
/* P-DMA test instance */
#define TEST_PDMA                      DW0

/* P-DMA test channel within P-DMA test instance */
#define TEST_CH                        5

/* Shift value for shifting an element to the upper 32-bit word of a 64-bit type */
#define SHIFT_TO_UPPER_32BIT_WORD      32

/* Number of bytes in a 32-bit word */
#define BYTES_PER_32_BIT_WORD          4

/* Fault interrupt priority */
#define IRQ_PRIORITY                   2

/* Shift value for CPU IRQ number ('intSrc' of cy_stc_sysint_t consists of CPU IRQ number and system IRQ number) */
#define CPU_IRQ_NUMBER_SHIFT           16

/* Calculates the absolute SRAM word address in a P-DMA instance based on the channel number and TargetSramWordType */
#define GET_SRAM_WORD_ADDRESS(ch, w)    ((ch) * 2 + (w))

/* Interval value */
#define INTERVAL_MS                    (1)

/* Fault Assignment */
#define CY_SYSFAULT_DW0_C_ECC  66
#define CY_SYSFAULT_DW0_NC_ECC 67
#define CY_SYSFAULT_DW1_C_ECC  68
#define CY_SYSFAULT_DW1_NC_ECC 69

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Each P-DMA channel has 2 SRAM words for which ECC error injection is possible */
typedef enum {
    TARGET_SRAM_WORD_0 = 0,
    TARGET_SRAM_WORD_1 = 1,
} TargetSramWordType;

/* Just a dummy DMA descriptor variable whose address will serve as the test value for ECC error injection */
cy_stc_dma_descriptor_t g_dummyDmaDescriptor;

/* Create a global volatile variable where the test access will store its data to prevent compiler optimization */
volatile uint32_t g_testReadData;

/* Flag that is set in the fault IRQ handler if any fault occurred and checked/cleared at other places */
bool g_faultIrqOccurred = false;

/* Flag that is set in the fault IRQ handler if a correctable ECC fault occurred in P-DMA instance #0 and
 * checked/cleared at other places */
bool g_faultIrqOccurredDwCorrectableEcc = false;

/* Flag that is set in the fault IRQ handler if a non-correctable ECC fault occurred in P-DMA instance #0 and
 * checked/cleared at other places */
bool g_faultIrqOccurredDwNonCorrectableEcc = false;

/*******************************************************************************
* Function Name: handleFaultIrq
********************************************************************************
* Summary:
* This handles the Fault Struct interrupt and will occur on P-DMA correctable 
* and non-correctable ECC faults.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
static void handleFaultIrq(void)
{
    uint32_t faultAddress;
    uint32_t faultInfo;
    cy_en_SysFault_source_t faultIdDwCorrectableEcc;
    cy_en_SysFault_source_t faultIdDwNonCorrectableEcc;

    printf("Fault IRQ Handler entered!\r\n");

    /* Get fault specific data from the registers */
    faultAddress = Cy_SysFault_GetFaultData(FAULT_STRUCT0, CY_SYSFAULT_DATA0);
    faultInfo = Cy_SysFault_GetFaultData(FAULT_STRUCT0, CY_SYSFAULT_DATA1);
    cy_en_SysFault_source_t errorSource = Cy_SysFault_GetErrorSource(FAULT_STRUCT0);

    /* Map P-DMA instance specific fault IDs to generic names for generic processing below */
    faultIdDwCorrectableEcc    = (TEST_PDMA == DW0) ? (cy_en_SysFault_source_t)CY_SYSFAULT_DW0_C_ECC  : (cy_en_SysFault_source_t)CY_SYSFAULT_DW1_C_ECC;
    faultIdDwNonCorrectableEcc = (TEST_PDMA == DW0) ? (cy_en_SysFault_source_t)CY_SYSFAULT_DW0_NC_ECC : (cy_en_SysFault_source_t)CY_SYSFAULT_DW1_NC_ECC;

    /* Check and display the fault information */
    if ((errorSource == faultIdDwCorrectableEcc) || (errorSource == faultIdDwNonCorrectableEcc))
    {
        if (errorSource == faultIdDwCorrectableEcc)
        {
            /* Set flag so that test code can check that the IRQ has occurred */
            g_faultIrqOccurredDwCorrectableEcc = true;
            printf("P-DMA correctable ECC fault detected:\r\n");
        }
        else
        {
            /* Set flag so that test code can check that the IRQ has occurred */
            g_faultIrqOccurredDwNonCorrectableEcc = true;
            printf("P-DMA non-correctable ECC fault detected:\r\n");
        }
        printf("- Word address: 0x%" PRIu32 "\r\n", faultAddress / BYTES_PER_32_BIT_WORD);
        printf("- ECC syndrome: 0x%" PRIu32 "\r\n", faultInfo);
    }
    else
    {
        printf("TEST ERROR: Unexpected fault source (0x%" PRIu32 ") detected!\r\n", (uint32_t)errorSource);
    }

    /* Set flag so that test code can check that the IRQ has occurred */
    g_faultIrqOccurred = true;

    Cy_SysFault_ClearStatus(FAULT_STRUCT0);
    Cy_SysFault_ClearInterrupt(FAULT_STRUCT0);
}

/*******************************************************************************
* Function Name: initFaultHandling
********************************************************************************
* Summary:
* This initialize fault handling to generate an IRQ on P-DMA correctable 
* and non-correctable ECC faults.
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
static void initFaultHandling(void)
{
    /* Only IRQ is needed as fault reaction in this example */
    cy_stc_SysFault_t faultStructCfg =
    {
        .ResetEnable   = false,
        .OutputEnable  = false,
        .TriggerEnable = false,
    };

    /* Set up fault struct and enable interrupt for P-DMA correctable and non-correctable ECC faults */
    Cy_SysFault_ClearStatus(FAULT_STRUCT0);
    if (TEST_PDMA == DW0)
    {
        Cy_SysFault_SetMaskByIdx(FAULT_STRUCT0, (cy_en_SysFault_source_t)CY_SYSFAULT_DW0_C_ECC);
        Cy_SysFault_SetMaskByIdx(FAULT_STRUCT0, (cy_en_SysFault_source_t)CY_SYSFAULT_DW0_NC_ECC);
    }
    else
    {
        Cy_SysFault_SetMaskByIdx(FAULT_STRUCT0, (cy_en_SysFault_source_t)CY_SYSFAULT_DW1_C_ECC);
        Cy_SysFault_SetMaskByIdx(FAULT_STRUCT0, (cy_en_SysFault_source_t)CY_SYSFAULT_DW1_NC_ECC);
    }
    Cy_SysFault_SetInterruptMask(FAULT_STRUCT0);
    if (Cy_SysFault_Init(FAULT_STRUCT0, &faultStructCfg) != CY_SYSFAULT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Set up the interrupt processing */
    cy_stc_sysint_t irqCfg =
    {
        .intrSrc = ((NvicMux3_IRQn << CPU_IRQ_NUMBER_SHIFT) | cpuss_interrupts_fault_0_IRQn),
        .intrPriority = IRQ_PRIORITY,
    };
    if (Cy_SysInt_Init(&irqCfg, &handleFaultIrq) != CY_SYSINT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    NVIC_EnableIRQ((IRQn_Type) NvicMux3_IRQn);
}

/*******************************************************************************
* Function Name: do64BitXorReduction
********************************************************************************
* Summary:
* This do reduction XOR operation on 64-bit value
*
* Parameters:
*  data64 - value
*
* Return:
*  uint8_t - XOR reduction result
*
*******************************************************************************/
static uint8_t do64BitXorReduction(uint64_t data64)
{
    /* Use Brian Kernighan algorithm to count the number of '1' bits in the provided 64-bit value */
    uint8_t countOneBits = 0;
    for (countOneBits = 0; data64 != 0; countOneBits++)
    {
        /* Clear the least significant bit set */
        data64 &= (data64 - 1);
    }

    /* If the number of bits set is even, the XOR reduction results in '0', or '1' otherwise */
    return ((countOneBits % 2) == 0) ? 0 : 1;
}

/*******************************************************************************
* Function Name: getParityForValue
********************************************************************************
* Summary:
* This calculate the P-DMA ECC parity for the provided 32-bit value 
* at the provided P-DMA SRAM word address
*
* Parameters:
*  channel - target channel within P-DMA instance #0 for which ECC parity 
*  shall be calculated
*  targetWord - target SRAM word in the given channel for which ECC parity 
*  shall be calculated (SRAM word #0 or #1)
*  value32 - value for which ECC parity shall calculated
*
* Return:
*  uint8_t - ECC parity
*
*******************************************************************************/
static uint8_t getParityForValue(uint8_t channel, TargetSramWordType targetWord, uint32_t value32)
{
    /* Constants for ECC parity calculation as per the Architecture TRM */
    static const uint64_t ECC_P[7] =
    {
        0x037F36DB22542AABull,
        0x05BDEB5A44994D35ull,
        0x09DDDCEE08E271C6ull,
        0x11EEBBA98F0381F8ull,
        0x21F6D775F003FE00ull,
        0x41FB6DB4FFFC0000ull,
        0x8103FFF8112C965Full,
    };

    /* The SRAM word address within a P-DMA instance is needed for calculating the parity */
    uint64_t wordAddr = GET_SRAM_WORD_ADDRESS(channel, targetWord);

    /* The 64-bit code word which is the basis for the ECC parity calculation is constructed as follows */
    uint64_t codeWord64 = value32 | (wordAddr << SHIFT_TO_UPPER_32BIT_WORD);

    /* Calculate each ECC parity bit individually according to the Architecture TRM */
    uint8_t parity = 0;
    for (uint32_t cnt = 0; cnt < (sizeof(ECC_P) / sizeof(ECC_P[0])); cnt++)
    {
        parity |= (do64BitXorReduction(codeWord64 & ECC_P[cnt]) << cnt);
    }

    return parity;
}
/*******************************************************************************
* Function Name: injectParity
********************************************************************************
* Summary:
* This Enable the injection of the provided ECC parity value for the provided 
* P-DMA channel and target SRAM word
*
* Parameters:
*  channel - target channel within the configured test P-DMA instance for which 
*  ECC parity shall be injected
*  targetWord - inject parity for SRAM word #0 or #1
*  parity - parity value to be injected
*
* Return:
*  none
*
*******************************************************************************/
static void injectParity(uint8_t channel, TargetSramWordType targetWord, uint8_t parity)
{
    uint32_t wordAddr;

    /* The SRAM word address within a P-DMA instance is needed for setting up the error injection */
    wordAddr = GET_SRAM_WORD_ADDRESS(channel, targetWord);

    /* Set the parity and target address */
    TEST_PDMA->ECC_CTL = _VAL2FLD(DW_ECC_CTL_PARITY, parity) | _VAL2FLD(DW_ECC_CTL_WORD_ADDR, wordAddr);

    /* Enable ECC injection */
    CY_REG32_CLR_SET(TEST_PDMA->CTL, DW_CTL_ECC_INJ_EN, 1);

    /* Inject the provided and configured parity by reading the current value and writing back the same value */
    if (targetWord == TARGET_SRAM_WORD_0)
    {
        uint32_t temp = TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA0;
        TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA0 = temp;
    }
    else
    {
        uint32_t temp = TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA1;
        TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA1 = temp;
    }

    /* Disable ECC injection now that parity has been injected and will get effective on next read of target word */
    CY_REG32_CLR_SET(TEST_PDMA->CTL, DW_CTL_ECC_INJ_EN, 0);
}
/*******************************************************************************
* Function Name: executeTestAccess
********************************************************************************
* Summary:
* This Makes a test access to our target SRAM word in the P-DMA with all 
* required steps before the access
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
static void executeTestAccess(void)
{
    /* Initialize the read data variable which might contain data from the previous access */
    g_testReadData = 0;
    /* Clear flags that are used to detect the IRQ occurrence */
    g_faultIrqOccurred = false;
    g_faultIrqOccurredDwCorrectableEcc = false;
    g_faultIrqOccurredDwNonCorrectableEcc = false;
    /* Make the test access */
    g_testReadData = (uint32_t) Cy_DMA_Channel_GetCurrentDescriptor(TEST_PDMA, TEST_CH);
}
/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function.
*
* Parameters:
*  none
*
* Return:
*  none
*
*******************************************************************************/
int main(void)
{
    /* Will hold the correct parity for the test value at the configured test location */
    uint8_t correctParity;

    /* Initialize the device and board peripherals */
    if (cybsp_init() != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize retarget-io to use the debug UART port */
    Cy_SCB_UART_Init(UART_HW, &UART_config, NULL);
    Cy_SCB_UART_Enable(UART_HW);
    cy_retarget_io_init(UART_HW);

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("****************** P-DMA ECC Error Injection Code Example ******************\r\n\r\n");

    initFaultHandling();

    /* Initialize ECC of the SRAM words for the DMA channel used for testing */
    TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA0 = 0;
    TEST_PDMA->CH_STRUCT[TEST_CH].SRAM_DATA1 = 0;

    /* This example will demonstrate ECC error injection for SRAM word #1 of a P-DMA channel. This SRAM word will
     * hold the current descriptor address which is normally configured via CH_CURR_PTR register of a channel */
    Cy_DMA_Channel_SetDescriptor(TEST_PDMA, TEST_CH, &g_dummyDmaDescriptor);

    /* Calculate the correct parity once. It will be re-used later for error injection purposes */
    correctParity = getParityForValue(TEST_CH, TARGET_SRAM_WORD_1, (uint32_t)&g_dummyDmaDescriptor);

    printf("Info about P-DMA test\r\n");
    printf("- Test value (descriptor address): 0x%" PRIu32 "\r\n", (uint32_t)&g_dummyDmaDescriptor);
    printf("- Correct ECC Parity:              0x%" PRIu8 "\r\n", correctParity);
    printf("\r\n");


    printf("Test step 1: Inject correct parity to prove correctness of ECC parity calculation\r\n");
    injectParity(TEST_CH, TARGET_SRAM_WORD_1, correctParity);
    executeTestAccess();
    Cy_SysLib_Delay(INTERVAL_MS);
    if (g_faultIrqOccurred)
    {
        printf("TEST ERROR: Unexpected fault occurred!\r\n");
    }
    else if (g_testReadData != (uint32_t)&g_dummyDmaDescriptor)
    {
        printf("TEST ERROR: Incorrect data read!\r\n");
    }
    else
    {
        printf("TEST OK!\r\n");
    }
    printf("\r\n");


    printf("Test step 2: Inject parity with 1-bit error to test correctable ECC fault\r\n");

    /* Set target value */
    Cy_DMA_Channel_SetDescriptor(TEST_PDMA, TEST_CH, &g_dummyDmaDescriptor);

    /* Flip 1 bit */
    injectParity(TEST_CH, TARGET_SRAM_WORD_1, correctParity ^ 0x01);
    executeTestAccess();
    Cy_SysLib_Delay(INTERVAL_MS);
    if (g_faultIrqOccurred == false)
    {
        printf("TEST ERROR: Fault IRQ has not occurred!\r\n");
    }
    else if (g_faultIrqOccurredDwCorrectableEcc == false)
    {
        printf("TEST ERROR: Fault IRQ has occurred, but not for the expected fault source!\r\n");
    }
    else if (g_testReadData != (uint32_t)&g_dummyDmaDescriptor)
    {
        printf("TEST ERROR: Read data has not been corrected by ECC logic!\r\n");
    }
    else
    {
        printf("TEST OK!\r\n");
    }
    printf("\r\n");
    /* Heal the corruption by simply setting the DMA descriptor again */
    Cy_DMA_Channel_SetDescriptor(TEST_PDMA, TEST_CH, &g_dummyDmaDescriptor);

    printf("Test step 3: Inject parity with 2-bit error to test non-correctable ECC fault\r\n");

    /* Flip 2 bits */
    injectParity(TEST_CH, TARGET_SRAM_WORD_1, correctParity ^ 0x03);
    executeTestAccess();
    Cy_SysLib_Delay(INTERVAL_MS);
    if (g_faultIrqOccurred == false)
    {
        printf("TEST ERROR: Fault IRQ has not occurred!\r\n");
    }
    else if (g_faultIrqOccurredDwNonCorrectableEcc == false)
    {
        printf("TEST ERROR: Fault IRQ has occurred, but not for the expected fault source!\r\n");
    }
    else
    {
        printf("TEST OK!\r\n");
    }
    printf("\r\n");
    /* Heal the corruption by simply setting the DMA descriptor again */
    Cy_DMA_Channel_SetDescriptor(TEST_PDMA, TEST_CH, &g_dummyDmaDescriptor);

    for (;;)
    {
    }
}


/* [] END OF FILE */

#include "flash_backend.h"
#include "stm32f10x_flash.h"
static int range_valid(unsigned bank, uint32_t offset, uint32_t size)
{ return bank<2u && offset<=CONFIG_STORE_BANK_SIZE && size<=CONFIG_STORE_BANK_SIZE-offset; }
static uint32_t address_of(unsigned bank, uint32_t offset)
{ return (bank==0u?FLASH_BACKEND_BANK0_ADDRESS:FLASH_BACKEND_BANK1_ADDRESS)+offset; }
static void prepare(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
}
static int flash_read(void *context, unsigned bank, uint32_t offset,
                      void *destination, uint32_t size)
{
    const volatile unsigned char *source;
    unsigned char *out=(unsigned char *)destination;
    (void)context;
    if(!range_valid(bank,offset,size) || (!destination && size!=0)) return 0;
    source=(const volatile unsigned char *)(uintptr_t)address_of(bank,offset);
    while(size--) *out++=*source++;
    return 1;
}
static int flash_erase(void *context, unsigned bank)
{
    uint32_t offset,address,i;
    const volatile uint16_t *page;
    (void)context;
    if(bank>=2u) return 0;
    prepare();
    for(offset=0;offset<CONFIG_STORE_BANK_SIZE;offset+=FLASH_BACKEND_PAGE_SIZE) {
        address=address_of(bank,offset);
        if(FLASH_ErasePage(address)!=FLASH_COMPLETE) { FLASH_Lock(); return 0; }
        page=(const volatile uint16_t *)(uintptr_t)address;
        for(i=0;i<FLASH_BACKEND_PAGE_SIZE/2u;i++)
            if(page[i]!=0xFFFFu) { FLASH_Lock(); return 0; }
    }
    FLASH_Lock(); return 1;
}
static int flash_program(void *context, unsigned bank, uint32_t offset, uint16_t value)
{
    uint32_t address;
    volatile uint16_t *target;
    FLASH_Status status;
    (void)context;
    if((offset&1u) || !range_valid(bank,offset,2)) return 0;
    address=address_of(bank,offset);
    target=(volatile uint16_t *)(uintptr_t)address;
    /* Require erased halfword: no second programming of existing data. */
    if(*target!=0xFFFFu) return 0;
    prepare();
    status=FLASH_ProgramHalfWord(address,value);
    FLASH_Lock();
    return status==FLASH_COMPLETE && *target==value;
}
void FlashBackend_Init(ConfigStoreBackend *backend)
{
    if(!backend) return;
    backend->context=0;
    backend->read=flash_read;
    backend->erase_bank=flash_erase;
    backend->program_halfword=flash_program;
}

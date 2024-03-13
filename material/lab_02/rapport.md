# Laboratoire 2

### André Costa

## Quelle est la différence entre ce driver et le Userspace platform driver with generic irq and dynamic memory?

### Help platform driver

│ CONFIG_UIO_PDRV_GENIRQ: │  
 │ │  
 │ Platform driver for Userspace I/O devices, including generic │  
 │ interrupt handling code. Shared interrupts are not supported. │  
 │ │  
 │ This kernel driver requires that the matching userspace driver │  
 │ handles interrupts in a special way. Userspace is responsible │  
 │ for acknowledging the hardware device if needed, and re-enabling │  
 │ interrupts in the interrupt controller using the write() syscall.

### Help dynamic memory

CONFIG_UIO_DMEM_GENIRQ: │  
 │ │  
 │ Platform driver for Userspace I/O devices, including generic │  
 │ interrupt handling code. Shared interrupts are not supported. │  
 │ │  
 │ Memory regions can be specified with the same platform device │  
 │ resources as the UIO_PDRV drivers, but dynamic regions can also │  
 │ be specified. │  
 │ The number and size of these regions is static, │  
 │ but the memory allocation is not performed until │  
 │ the associated device file is opened. The │  
 │ memory is freed once the uio device is closed.

## Réponse

- Avec le driver UIO_PDRV_GENIRQ, on doit mapper les zones memoires qui nous intéressent lors du build time.
- Avec le driver UIO_DMEM_GENIRQ, on peut mapper les zones memoires qui nous intéressent lors du runtime.

## DTS

### Explication des lignes

```dts
#include <dt-bindings/interrupt-controller/irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
```

- Inclure les headers pour les interruptions.

```dts
drv2024 {
    compatible = "drv2024"; # Indique le nom du driver
    reg = <0xFF200000 0x1000>; # Mapper la page où se trouvent les boutons
    interrupts = <GIC_SPI 41 IRQ_TYPE_EDGE_RISING>; # Indique le numéro de l'interruption. SPI = Shared Peripheral Interrupt et pas Serial Peripheral Interface
    interrupt-parent = <&intc>; #Indique sur quel bloc on reçoit l'interrupt
};
```

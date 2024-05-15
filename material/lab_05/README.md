# DRV - Labo 5 

André Costa

# Exercice 1

Le code peut être trouvé [dans le répertoire show_number](./show_number/)

Compilation du code:

```bash
cd show_number
make
mv show_number.ko <path_to_export_folder>
```

Compilation du code de test:

```bash
cd show_number/test
arm-linux-gnueabihf-gcc -Wall -Wextra -o <path_to_export_folder>/show_number_test show_number_test.c
```

Pour tester le module dans la DE1-SoC:

```bash
insmod show_number.ko
./show_number_test
rmmod show_number
```

# Exercice 2

Le code peut être trouvé [dans le répertoire show_number-2](./show_number-2/)

Compilation du code:

```bash
cd show_number-2
make
mv show_number.ko <path_to_export_folder>
```

Compilation du code de test (c'est le même que pour l'exercice 1):

```bash
cd show_number/test
arm-linux-gnueabihf-gcc -Wall -Wextra -o <path_to_export_folder>/show_number_test show_number_test.c
```

Pour tester le module dans la DE1-SoC:

```bash
insmod show_number.ko
./show_number_test
# Presser sur KEY1 pour arrêter l'affichage
# Vérification des valeurs à travers sysfs - répertoire /sys/devices/platform/ff200000.drv2024/

cat /sys/devices/platform/ff200000.drv2024/display_count # Affiche nombre de valeurs affichées
cat /sys/devices/platform/ff200000.drv2024/display_time # Affiche le temps d'affichage de chaque valeur
cat /sys/devices/platform/ff200000.drv2024/fifo_len # Affiche la taille de la fifo
cat /sys/devices/platform/ff200000.drv2024/fifo # Affiche les valeurs dans la fifo
echo 500 > /sys/devices/platform/ff200000.drv2024/display_time # Change le temps d'affichage

./show_number_test # Pour relancer l'affichage (ajoute des nouvelles valeurs à la fifo

rmmod show_number
```

# Exercice 3

Le code peut être trouvé [dans le répertoire led-controller](./led-controller)

Compilation du code:

```bash
cd led-controller
make
mv led_controller.ko <path_to_export_folder>
```

Pour tester le module:

```bash
insmod led_controller.ko
# Utiliser sysfs pour modifier la valeur et le mode -- répertoire /sys/devices/platform/ff200000.drv2024/config
rmmod led_controller
```

# Exercice 4

## Questions

### Est-ce que cela pose problème avec l’implémentation courante ?

Oui, car on ne peut pas utiliser un mutex dans un context IRQ ceci car le mutex met le processus en mode sleep et lorsque le kernel et en mode IRQ, il ne peut pas passer en mode sleep.

### Si oui, quelles sont les possibilités pour corriger le problème ?

Pour corriger ce problème, on peut utiliser un spinlock à la place du mutex. Le spinlock ne met pas le processus en mode sleep, il fait une attente active jusqu'à que le lock soit disponible.

## Code

Le code peut être trouvé [dans le répertoire led-controller-4](./led-controller-4)

Compilation du code:

```bash
cd led-controller-4
make
mv led_controller.ko <path_to_export_folder>
```

Test du module:

```bash
insmod led_controller.ko
# Modifier les valeurs des switches
# Presser sur la touche KEY0 pour modifier la valeur des leds
rmmod led_controller
```

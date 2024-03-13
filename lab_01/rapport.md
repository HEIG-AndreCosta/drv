# Laboratoire 1 - DRV

### André Costa

---

## Exercice 1

### Expliquez les différences entre les lignes ci-dessus.

```shell
md.b 0x80008000 0x1
80008000: 46    F
md.w 0x80008000 0x1
80008000: 4c46    FL
md.l 0x80008000 0x1
80008000: eb004c46    FL..
```

- Ici, on lit soit 1 byte (`md.b`), 2 bytes(`md.w`) ou 4 bytes(`md.l`) depuis l'adresse 0x80008000;

### Utilisez la commande md pour lire la valeur binaire écrite avec les switches et écrivez-la sur les LEDs.

```uboot
SOCFPGA_CYCLONE5 # md.l 0xFF200040 1
ff200040: 00000003    ....
SOCFPGA_CYCLONE5 # mw.l 0xFF200000 00000003 1
```

### Qu’est-ce qui se passe si vous essayez d’accéder à une adresse qui n’est pas alignée (par exemple 0x01010101) et pourquoi ?

- Le système redémarre car on ne peut que lire des adresses alignées.

## Exercice 2

```uboot
SOCFPGA_CYCLONE5 # setenv ex2 "while true; do mw.b 0xFF200030 49 2; mw.b 0xFF200020 49 4; sleep 1; mw.b 0xFF200030 36 2; mw.b 0xFF200020 36 4; sleep 1; done;"
SOCFPGA_CYCLONE5 # run ex2
```

---

# [Exercice 3](./ex3.c)

## Compilation

```shell
arm-linux-gnueabihf-gcc ex3. -o <path_to_export_folder>/ex3
```

## Lancement

Dans la DE1

```shell
./ex3
``` 

- Regarder les leds

# [Exercice 4](./ex4.c)

## Compilation

```shell
arm-linux-gnueabihf-gcc ex4.c -o <path_to_export_folder>/ex4
```

Dans la DE1
```shell
./ex4
``` 

- Presser les touches KEY0 et KEY1 pour faire défiler le texte

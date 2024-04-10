# Laboratoire 3 - DRV

#### André Costa

# Exercice 1

```shell
# man mknod
MKNOD(1)                                                               User Commands                                                              MKNOD(1)

NAME
       mknod - make block or character special files

SYNOPSIS
       mknod [OPTION]... NAME TYPE [MAJOR MINOR]

DESCRIPTION
       Create the special file NAME of the given TYPE.

       Mandatory arguments to long options are mandatory for short options too.

       -m, --mode=MODE
              set file permission bits to MODE, not a=rw - umask

       -Z, --context=CTX
              set the SELinux security context of NAME to CTX

       --help display this help and exit

       --version
              output version information and exit

       Both  MAJOR  and MINOR must be specified when TYPE is b, c, or u, and they must be omitted when TYPE is p.  If MAJOR or MINOR begins with 0x or 0X,
       it is interpreted as hexadecimal; otherwise, if it begins with 0, as octal; otherwise, as decimal.  TYPE may be:

       b      create a block (buffered) special file

       c, u   create a character (unbuffered) special file

       p      create a FIFO

```

Pour créer un fichier virtuel de type caractère on peut utiliser:

```shell
mknod -m <MODE> <NAME> c <MAJOR> <MINOR>
```

Pour savoir la valeur de MAJOR et MINOR de `/dev/random` on peut utiliser:

```shell
ls -l /dev/random
crw-rw-rw- 1 root root 1, 8 Jan  1 00:00 /dev/random
```

Le couple MAJOR MINOR est `1, 8`.

Pour le mode, on peut aussi utiliser le même mode que `/dev/random`:

Le mask correspondant à `rw-rw-rw-` est 0666 en octal.

Je nomme mon device `mondev`.

La commande finale devient donc:

```shell
mknod -m 0666 /dev/mondev c 1 8
```

En utilisant `ls -l /dev/mondev` on peut voir que le fichier a été créé.

```shell
# ls -l /dev/mondev
crw-rw-rw- 1 root root 1, 8 Jan  1 00:13 /dev/mondev
```

Vu que le major et le mineur sont les mêmes que `/dev/random`, le même driver s'occupera de notre device.
En l'utilisant on peut voir que nous avons des valeurs aléatoires.

```shell
# cat /dev/mondev
�xe��Կ�<X�(k7�����֒O�:+���ٷ
@N|��^CFu�.������f�+ұLH8E	s�� 0�5j1u�X��R�v
```

# Exercice 2

```shell
$ cat /proc/devices | grep 188
188 ttyUSB
```

# Exercice 3

```shell
$ find /sys -name "ttyUSB0"
/sys/devices/pci0000:00/0000:00:14.0/usb1/1-4/1-4:1.0/ttyUSB0
/sys/devices/pci0000:00/0000:00:14.0/usb1/1-4/1-4:1.0/ttyUSB0/tty/ttyUSB0
/sys/bus/usb-serial/devices/ttyUSB0
/sys/bus/usb-serial/drivers/ftdi_sio/ttyUSB0
```

En reprenant le premier chemin, on peut voir que le driver utilisé est `ftdi_sio`.

```shell
$ ll /sys/devices/pci0000:00/0000:00:14.0/usb1/1-4/1-4:1.0/ttyUSB0
total 0
lrwxrwxrwx 1 root root    0 Apr 10 10:50 driver -> ../../../../../../../bus/usb-serial/drivers/ftdi_sio # Driver utilisé
--w------- 1 root root 4.0K Apr 10 11:04 event_char
-rw-r--r-- 1 root root 4.0K Apr 10 11:04 latency_timer
-r--r--r-- 1 root root 4.0K Apr 10 10:50 port_number
drwxr-xr-x 2 root root    0 Apr 10 11:02 power
lrwxrwxrwx 1 root root    0 Apr 10 10:50 subsystem -> ../../../../../../../bus/usb-serial
drwxr-xr-x 3 root root    0 Apr 10 10:50 tty
-rw-r--r-- 1 root root 4.0K Apr 10 10:50 uevent
```

Et avec lsmod on peut voir que le module `ftdi_sio` est bien chargé.

```shell
lsmod
Module                  Size  Used by
ftdi_sio               69632  0
usbserial              69632  1 ftdi_sio
```

Et on voit que le driver `ftdi_sio` utilise le module `usbserial`.

# Exercice 4

Après avoir modifié la fonction `empty_exit` pour qu'elle utilise la fonction `pr_info` à la place de `pr_debug`

```c
static void __exit empty_exit(void)
{
    //pr_debug("Good bye!\n");
	pr_info("Good bye!\n");
}
```

On voit les deux messages dans les logs:

```shell
$ make
$ sudo insmod empty.ko
$ sudo dmesg | tail -n 1
[10274.107881] Hello there!
$ sudo rmmod empty
$ dmesg | tail -n 2
[10274.107881] Hello there!
[10315.914668] Good bye!
```

# Exercice 5

- Lancer le driver `flifo` avec `insmod`

```shell
$ sudo insmod flifo.ko
```

- Crée un device avec `mknod` avec le major `97` pour que le driver `flifo` s'en occupe.

```shell
$ sudo mknod -m 0666 /dev/mynode c 97 0
$ ls -la /dev/mynode
crw-rw-rw- 1 root root 97, 0 Apr 10 11:36 /dev/mynode
```

- Lecture et écriture dans le device

```shell
$ echo "1" >> /dev/mynode
$ echo "2" >> /dev/mynode
$ cat /dev/mynode
1%
$ cat /dev/mynode
2%
```

- Changement de Mode

- Le 1074014977 a été trouvé dans `dmesg` et indique CHANGE_MODE,
- Le 1 dans flifo.h et est utilisé pour changer en mode LIFO

```shell
$ ./ioctl /dev/mynode 1074014977 1
$ echo "1" >> /dev/mynode
$ echo "2" >> /dev/mynode
$ cat /dev/mynode
2%
$ cat /dev/mynode
1%
```

- Déchargement du device

```shell
$ sudo rmmod flifo
$ sudo dmesg
[11645.484760] FLIFO ready!
[11645.484763] ioctl FLIFO_CMD_RESET: 11008
[11645.484764] ioctl FLIFO_CMD_CHANGE_MODE: 1074014977
[12728.868418] FLIFO done!
```

## Modérnisation du code

- Pour la modérnisation du code, je m'enregistre dans la plateforme misc.

```c
const static struct file_operations flifo_fops = {
	.owner = THIS_MODULE,
	.read = flifo_read,
	.write = flifo_write,
	.unlocked_ioctl = flifo_ioctl,
};
static struct miscdevice flifo_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &flifo_fops,
};
static int __init flifo_init(void)
{
	next_in = 0;
	nb_values = 0;
	mode = MODE_FIFO;

	//	register_chrdev(MAJOR_NUM, DEVICE_NAME, &flifo_fops);
	int ret = misc_register(&flifo_miscdev);
	pr_info("FLIFO ready!\n");
	pr_info("ioctl FLIFO_CMD_RESET: %u\n", FLIFO_CMD_RESET);
	pr_info("ioctl FLIFO_CMD_CHANGE_MODE: %lu\n", FLIFO_CMD_CHANGE_MODE);

	return 0;
}
static void __exit flifo_exit(void)
{
	//unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
	misc_deregister(&flifo_miscdev);
	pr_info("FLIFO done!\n");
}
```

- Une fois ces changements effectués, lors de l'insertion du module, on peut voir le fichier `/dev/flifo` créé automatiquement.

```shell
ls -la /dev/flifo
crw------- 1 root root 10, 119 avr 10 22:01 /dev/flifo
```

Pour éviter de devoir faire un sudo à chaque fois que je veux écrire dans le fichier, je peux changer les permissions du fichier.

```shell
sudo chmod 666 /dev/flifo
```

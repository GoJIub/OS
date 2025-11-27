## 1. Запуск процесса `parent`

```text
execve("./parent", ["./parent"], 0xffffee4c5eb0 /* 76 vars */) = 0
```

* Вызов системного вызова `execve`: запуск исполняемого файла `./parent`.
* Это соответствует тому, что запускается программа `parent` из шелла.

Дальше большой блок с `openat`, `mmap`, `read`, `mprotect` и т.п. над библиотеками (`librt.so.1`, `libpthread.so.0`, `libc.so.6`) — это **динамический загрузчик** подтягивает

* `librt` (для POSIX-семафоров),
* `libpthread` (для потоков/семафоров),
* `libc` (стандартная библиотека).

Это служебная работа, не код напрямую.

---

## 2. Подготовка файловой системы для shared memory

```text
statfs("/dev/shm/", {f_type=TMPFS_MAGIC, ...}) = 0
```

* Проверяется файловая система `/dev/shm` — это tmpfs, где `shm_open` создаёт объекты.
* Это подготовка к работе с `shm_open` в  `parent.c`.

---

## 3. Создание сегментов shared memory (аналог двух pipe’ов)

```text
openat(AT_FDCWD, "/dev/shm/shm_data", O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0666) = 3
openat(AT_FDCWD, "/dev/shm/shm_err",  O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC, 0666) = 4
```

* Это реализация `shm_open("/shm_data", O_CREAT | O_RDWR, 0666)` и `shm_open("/shm_err", ...)` из `parent.c`.
* В реальности `shm_open` создаёт файлы в `/dev/shm/` с таким именем.

```text
ftruncate(3, 1088)                      = 0
ftruncate(4, 4128)                      = 0
```

* `ftruncate` задаёт размер объектов:

  * `1088` байт ≈ `sizeof(struct shmseg)`,
  * `4128` байт ≈ `sizeof(struct shmerr)`.
* Это соответствует:

```c
ftruncate(fd_data, sizeof(struct shmseg));
ftruncate(fd_err,  sizeof(struct shmerr));
```

---

## 4. Отображение этих файлов в память через `mmap`

```text
mmap(NULL, 1088, PROT_READ|PROT_WRITE, MAP_SHARED, 3, 0) = 0xffffa7b6b000
mmap(NULL, 4128, PROT_READ|PROT_WRITE, MAP_SHARED, 4, 0) = 0xffffa7b69000
close(3)                                = 0
close(4)                                = 0
```

* Это — реализация в ядре вот этого кода:

```c
struct shmseg *shm_data = mmap(NULL, sizeof(struct shmseg),
                               PROT_READ | PROT_WRITE, MAP_SHARED, fd_data, 0);
struct shmerr *shm_err = mmap(NULL, sizeof(struct shmerr),
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd_err, 0);
close(fd_data);
close(fd_err);
```

* `MAP_SHARED` — изменения видны и родителю, и дочернему процессу.
* После `mmap` файловые дескрипторы закрываются, потому что дальше работа идёт через адреса в памяти, а не через файловый интерфейс.

---

## 5. Создание дочернего процесса (fork)

```text
clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD,
      child_tidptr=0xffffa7b6c0e0) = 4477
```

* Это системный вызов `clone`, который в user-space обёрнут как `fork()`:

```c
pid_t pid = fork();
```

* Возвращаемое значение `4477` — PID дочернего процесса.

---

## 6. Ввод/вывод в родителе (общение с пользователем)

```text
fstat(1, {st_mode=S_IFCHR|0620, ...}) = 0
...
write(1, "Введите имя файла...", 34) = 34
read(0, "efwvd.txt\n", 1024)            = 10
```

* `write(1, ...)` — вывод в `stdout` строки `Введите имя файла`.
* `read(0, ...)` — чтение с `stdin`, это  `getline(&buffer, ...)`:

```c
printf("Введите имя файла: ");
getline(&buffer, &bufsize, stdin);
```

Дальше повторяется тот же паттерн: `write(1, "Введите строку ...")` / `read(0, "...")` — это цикл чтения строк пользователя:

```c
printf("Введите строку...");
getline(&buffer, &bufsize, stdin);
```

Примеры:

```text
write(1, "Введите строку ...", 84) = 84
read(0, "dasda\n", 1024)          = 6
...
read(0, "sdfASD;\n", 1024)        = 8
...
read(0, "\n", 1024)               = 1   // пустая строка -> завершение ввода
```

---

## 7. Семафоры и взаимодействие с дочерним процессом

В логе:

```text
futex(0xffffa7b6b420, FUTEX_WAKE, 1)    = 1
```

Эти вызовы `futex` соответствуют всем операциям `sem_wait` / `sem_post`.
POSIX-семафоры внутри используют `futex()` как примитив ядра.

Типичная последовательность:

1. Родитель вызывает `sem_wait(&shm_data->sem_empty)` → в ядре может быть `futex(..., FUTEX_WAIT, ...)` или просто манипуляция счётчиком.
2. После записи строки в `shm_data->buf` родитель делает `sem_post(&shm_data->sem_full)`, что приводит к `futex(..., FUTEX_WAKE, 1)` — пробуждает ждущий процесс (child).
3. Аналогично child после чтения делает `sem_post(&shm_data->sem_empty)`.

Именно вот эти `futex` показывают синхронизацию обмена строками между процессами через разделяемую память.

---

## 8. Ожидание завершения дочернего процесса

```text
wait4(-1, NULL, 0, NULL)                = 4477
--- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=4477, ...} ---
```

* `wait4` — это `wait(NULL);` в `parent.c`:

```c
wait(NULL); // ждем завершения дочернего процесса
```

* Система возвращает PID завершившегося дочернего процесса `4477` и сигнал `SIGCHLD`, который сообщает о его завершении.

---

## 9. Вывод лога ошибок

```text
write(1, "\n=== Лог ошибок (от child) ===\n", 42) = 42
write(1, "Error: строка должна оканчиваться ...\n", 90) = 90
```

* Это та часть `parent.c`, где после `wait(NULL)` печатается содержимое `shm_err->log`:

```c
if (strlen(shm_err->log) > 0)
    printf("\n=== Лог ошибок (от child) ===\n%s", shm_err->log);
```

* Все сообщения `Error: ...` туда предварительно записал дочерний процесс через shared memory и защищённый семафором `sem_err`.

---

## 10. Освобождение shared memory и завершение

```text
munmap(0xffffa7b6b000, 1088)            = 0
munmap(0xffffa7b69000, 4128)            = 0
unlinkat(AT_FDCWD, "/dev/shm/shm_data", 0) = 0
unlinkat(AT_FDCWD, "/dev/shm/shm_err", 0) = 0
exit_group(0)                           = ?
+++ exited with 0 +++
```

* `munmap(...)` — соответствуют:

```c
munmap(shm_data, sizeof(struct shmseg));
munmap(shm_err, sizeof(struct shmerr));
```

* `unlinkat("/dev/shm/shm_data")`, `unlinkat("/dev/shm/shm_err")` — это:

```c
shm_unlink(shm_name_data);
shm_unlink(shm_name_err);
```

Удаление объектов shared memory из `/dev/shm`, как того требует задание.

* `exit_group(0)` — нормальное завершение процесса `parent` с кодом возврата `0`.
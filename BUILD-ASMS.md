# Compilazione di Codebase Memory per AUP-SMS

Questa procedura genera il binario nativo con **CBM Lens incorporato** usando
lo script ufficiale `scripts/build.sh` del repository.

La build senza `--with-ui` non contiene l'interfaccia CBM Lens e non deve
essere usata da AUP-SMS.

## Requisiti comuni

- Git.
- Node.js 22 o successivo e npm, necessari per compilare CBM Lens.
- Una copia locale completa di questo repository.

Verifica Node.js e npm:

```bash
node --version
npm --version
```

## Windows

### 1. Installare e aprire MSYS2 CLANG64

Installare [MSYS2](https://www.msys2.org/), quindi aprire dal menu Start il
terminale **MSYS2 CLANG64**. Non eseguire la build da PowerShell, Prompt dei
comandi o da un terminale MSYS diverso da CLANG64.

Nel terminale CLANG64 installare i prerequisiti:

```bash
pacman -Syu
pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-zlib make python unzip zip
```

Se il primo aggiornamento richiede la chiusura del terminale, riaprire
**MSYS2 CLANG64** e ripetere i due comandi.

Verificare la toolchain:

```bash
clang --version
clang++ --version
make --version
node --version
npm --version
```

### 2. Compilare il binario con CBM Lens

```bash
cd /c/laragon/www/CodebaseMemory/codebase-memory-mcp-fork

scripts/build.sh \
  --with-ui \
  --version 0.12.0-asms.cr27.1 \
  BUILD_DIR=build/asms \
  CC=clang \
  CXX=clang++

cp -f build/asms/codebase-memory-mcp.exe build/codebase-memory.exe
```

Il binario usato da AUP-SMS sarà:

```text
C:\laragon\www\CodebaseMemory\codebase-memory-mcp-fork\build\codebase-memory.exe
```

Verificarlo:

```bash
./build/codebase-memory.exe --version
ls -lh ./build/codebase-memory.exe
sha256sum ./build/codebase-memory.exe
```

### 3. Configurare AUP-SMS su Windows

Nel `.env` di AUP-SMS:

```dotenv
ASMS_CODEBASE_MEMORY_BINARY=C:\laragon\www\CodebaseMemory\codebase-memory-mcp-fork\build\codebase-memory.exe
```

`ASMS_CODEBASE_MEMORY_BINARY_SHA256` e `ASMS_CODEBASE_MEMORY_VERSION` sono pin
opzionali. Se le variabili non sono presenti nel `.env`, AUP-SMS non esegue i
relativi controlli. Se sono presenti, i controlli rimangono rigorosi.

Dopo una modifica del `.env`:

```powershell
cd C:\laragon\www\aup-sms
php artisan config:clear
```

## Ubuntu

### 1. Installare i prerequisiti

```bash
sudo apt-get update
sudo apt-get install -y build-essential zlib1g-dev git python3 unzip zip
```

Installare Node.js 22 o successivo, quindi verificare:

```bash
gcc --version
g++ --version
make --version
node --version
npm --version
```

### 2. Compilare il binario con CBM Lens

Sostituire il percorso seguente con la directory effettiva del repository:

```bash
cd /opt/codebase-memory-mcp-fork

scripts/build.sh \
  --with-ui \
  --version 0.12.0-asms.cr27.1 \
  BUILD_DIR=build/asms \
  CC=gcc \
  CXX=g++

cp -f build/asms/codebase-memory-mcp build/codebase-memory
chmod 0755 build/codebase-memory
```

Verificare il risultato:

```bash
./build/codebase-memory --version
ls -lh ./build/codebase-memory
sha256sum ./build/codebase-memory
```

### 3. Configurare AUP-SMS su Ubuntu

Nel `.env` usare il percorso assoluto del binario:

```dotenv
ASMS_CODEBASE_MEMORY_BINARY=/opt/codebase-memory-mcp-fork/build/codebase-memory
```

Assicurarsi che l'utente del servizio PHP/web possa eseguire il file e
attraversare tutte le directory del percorso. Quindi svuotare la configurazione
Laravel:

```bash
cd /percorso/aup-sms
php artisan config:clear
```

## Build senza versione fissata

Il parametro `--version` riguarda la versione incorporata nel binario. Per una
build di sviluppo può essere omesso:

```bash
scripts/build.sh --with-ui BUILD_DIR=build/asms CC=clang CXX=clang++
```

Su Ubuntu sostituire `clang` e `clang++` con `gcc` e `g++` se si usa GCC.

## Risoluzione dei problemi

- `ui.no_assets`: il binario è stato compilato senza `--with-ui`; ricompilarlo
  seguendo questa procedura.
- `node: command not found` o `npm: command not found`: Node.js non è visibile
  nel terminale usato per la build.
- `zlib.h: No such file or directory`: installare il pacchetto zlib previsto
  per il sistema operativo.
- Impossibile sostituire il binario: arrestare prima i processi CBM che stanno
  usando quel file, quindi ripetere la copia.
- Dopo aver sostituito il binario, riavviare il supervisore CBM da AUP-SMS.

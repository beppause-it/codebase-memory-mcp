# Handoff ASMS — adozione stop CR38 e conflitto CR37/CR39

29 ottobre 2026. Nessuna modifica al runtime ASMS effettuata dal team CBM.

## Incidente

Il richiedente riporta il rifiuto di `config set auto_index false` durante
NativeLensDaemonControl.startOnce: daemon CR37 permanente PID33784, fingerprint
`ca5f7411624945cf79c1addcc1c0400c1136d9c5fc4e21854e7df11789d327e9`,
client CR39 `9936986cfb63c1fe8944607492f0c16165359644003a4d202a7e51380e9fd866`.
Il rifiuto delle operazioni operative tra build diverse resta intenzionale.
La capability cross-build di CR38 riguarda `daemon stop`, non autorizza
config/start/query misti e non ferma automaticamente altri client.

## Blocco consumer verificato nei sorgenti

`packages/aup-modules/asms-catalog/src/Infrastructure/CodeAnalysis/NativeLensDaemonControl.php`:

- stop() richiede `daemon status --json` con allowedExitCodes `[0, 3]`;
- accetta solamente status `active` prima di inviare lo stop;
- il daemon nativo restituisce invece `build_conflict` / exit5 quando osservato
  da un'altra build, conservando PID, build e client;
- pertanto il controllo ASMS fallisce PRIMA di invocare la primitiva cross-build.

## Adeguamento richiesto al team ASMS

Nel solo percorso amministrativo di stop, accettare exit5 come stato osservabile
e trattare `build_conflict` come daemon presente, preservandone la diagnostica.
Non estendere tale tolleranza ad assertReady/start/config/query o ai controlli
di qualificazione. Invocare il medesimo EXE CR39 configurato con
`daemon stop --json`; non selezionare un vecchio EXE e non usare taskkill.

Mantenere la gestione esplicita di busy/exit6, stopping, not_running e degli
errori IPC. Nessun successo STOPPED prima della verifica di uscita/rilascio
endpoint. Gestire PID/build sostituiti durante il polling senza un secondo
stop automatico verso un processo diverso. Solo dopo l'arresto verificato
eseguire configurazione e avvio della build nuova.

Usare esattamente lo stesso contesto HOME/USERPROFILE, cache e runtime che
ospita il daemon. Il runtime Lens dichiarato dal consumer è runtime_root/lens;
indexing e il runtime account predefinito sono contesti distinti. Un daemon
preesistente in altro contesto può richiedere una migrazione esplicita;
`not_running` in un contesto non certifica che il PID riportato sia terminato.
Non cancellare lock e non cercare/terminare processi genericamente per nome.

## Verifiche di questa analisi

Lettura CIM conferma il PID33784 con percorso build/cr37-native e argomenti
daemon permanente. Le interrogazioni CR39 dal contesto di esecuzione disponibile
qui non hanno rintracciato l'endpoint di quel processo (status not_running nei
contesti Lens/indexing/account interrogati). Nessuno stop o kill eseguito;
nessun arresto o migrazione reale dichiarati. Non attribuire tali risultati
alla liveness del processo, né considerarli un collaudo consumer riuscito.

Il produttore è già qualificato Windows in fixture private: CR39 include CR38;
CR38 finale ha fermato cooperativamente il vero CR36 distribuito, ha mantenuto
busy con un client reale e ha permesso l'avvio successivo della build nuova.
Per dettagli vedere CR-38-cbm-delivery.md e CR-39-cbm-delivery.md.

Test owner richiesti: status conflitto con client0, busy con client attivo,
stop/riprova/uscita verificata, nuovo avvio, cambio PID/build, contesto errato e
propagazione integrale di code/message/status/stdout/stderr. Non modificare o
rilanciare i job falliti storici sulla sola base di questo incidente.

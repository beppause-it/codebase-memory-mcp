# CR-39 — Schemi MCP coerenti con i limiti effettivi HTTP

## 0850 — Richiesta

29 ottobre2026. Owner CodebaseMemory/codebase-memory-mcp-fork. Stato PROPOSED.
Classificazione verified-bug / contract-change / hardening.
Richiesta umana: correggere il contratto dei tool, chiarendo i motivi dei
rifiuti. Non rimuovere controlli o aumentare limiti per aggirare l'errore.
Blocca collaudo affidabile tools/list -> tools/call nel profilo ASMS HTTP.
Separata da CR-37 (protocollo diagnostico), CR-38 (stop cross-version) e dal
binding ASMS scaduto. Non richiede di ricreare agenti, Team, workspace o indici.

## Evidenza riprodotta

Baseline binario0.12.0-asms.cr37.1; fingerprint riportato nel conflitto utente
ca5f7411624945cf79c1addcc1c0400c1136d9c5fc4e21854e7df11789d327e9.
Origine diagnostica CBM allegato0309fc06 conferma component_versioncr37.1;
nessun nuovo checksum/sorgentecommit e stato verificato durante questi probe.
Rapporto ASMS docs/117-0840-verifica-argomenti-tool-cbm.md, review indipendente
APPROVED. tools/list realeHTTP200 pubblica6tool; configurazione locale senza
override max_results/max_depth, default C effettivi100/8.

| Probe | Schema pubblicato | Risposta reale |
|---|---|---|
| query_graph max_rows0 |minimum0, esplicitamente ammesso|400 TOOL_ARGUMENTS_REJECTED|
| query_graph max_rows200 |default200|400 TOOL_ARGUMENTS_REJECTED|
| query_graph max_rows1 oppure100 |ammesso|400 SOURCE_NOT_ACTIVE|
| trace_path depth15 |maximum15|400 TOOL_ARGUMENTS_REJECTED|
| get_graph_schema source_id stringa7 |ammesso|400 SOURCE_NOT_ACTIVE|
| get_graph_schema source_id numero7 |non ammesso|string richiesto, rifiuto corretto|
| get_graph_schema diagnostics null |non ammesso|rifiuto corretto|

SOURCE_NOT_ACTIVE dimostra solo che il filtro argomenti e stato superato,
non che il grafo sia disponibile. I probe sono read-only, nessun binding,
limite, indice o preset modificato. Query usata costante:
MATCH (n) RETURN n LIMIT 1. La precisa chiamata storica Agent OS non e nota:
non attribuire automaticamente il precedente errore a uno di questi valori.

## Perche i rifiuti hanno motivo

Il profilo HTTP applica ceiling di risultati, profondita e risposta, oltre a
tipi, required, proprieta, path e sola lettura. Queste protezioni sono valide.
Il bug e annunciare valori/schema/default incompatibili con quelle protezioni.

Codice owner integrations/aup-sms/mcp-http/src/mcp_http.c:
- tool_schema528-580 deriva dal nativo e aggiunge source_id, ma non adatta
  minimum/default/maximum ai ceiling effettivi del profilo configurato;
- config349-353 imposta max_results100, max_depth8 come default;
- integer_ceiling_valid1066-1078 richiede n>0 e n<=results per max_rows/limit,
  depth<=cfg.depth e max_output_tokens<=response_max/4;
- args_valid1098 e execute_tool_call1743-1745 restituiscono il rifiuto generico
  prima della ricerca del binding e prima dell'audit applicativo.

Riproduzione sorgente dalla root owner:
```powershell
rg -n 'tool_schema|integer_ceiling_valid|args_valid|ASMS_DEFAULT_RESULTS|TOOL_ARGUMENTS_REJECTED' integrations/aup-sms/mcp-http/src/mcp_http.c
```
Esito osservato: schema e validatore usano limiti differenti. Il proxy ASMS
ProjectCbmHttpProxy inoltra getContent senza ricostruire gli argomenti.

## Riproduzione HTTP isolata e accettazione

Preparare un runtime di test autenticato con max_results100/max_depth8,
nessun binding per source7, poi initialize, notifications/initialized,
tools/list e tools/call, rispettando protocollo2025-06-18, sessione se richiesta,
Content-Type application/json e Accept application/json, text/event-stream.
Mai stampare bearer o usare sorgenti reali per modifiche.
```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"query_graph","arguments":{"source_id":"7","query":"MATCH (n) RETURN n LIMIT 1","max_rows":200}}}
```
Ripetere con max_rows0/1/100 e trace_path source_id7,function_name main,depth15.
Prima della correzione si osservano i codici della tabella. Con un binding
sintetico valido collaudare anche esecuzione riuscita, non solo il filtro.

1. Schema HTTP generato dai limiti effettivi della stessa configurazione usata
   dal validatore: default/min/max non devono promettere valori rifiutati solo
   dal profilo HTTP. Gestire anche limiti non predefiniti e reload configurato.
2. Verificare tutti i tool e gli interi limit, max_rows, semantic_limit, depth,
   max_output_tokens, oltre a vincoli specifici del profilo. Non basta correggere
   un solo parametro o inserire100/8 come nuove costanti negli schemi.
3. Distinguere argomenti invalidi da vincoli semantici o policy: valore conforme
   allo schema non garantisce query sensata, path esistente o binding attivo.
   Non eliminare tali verifiche e non convertire un rifiuto in risultato vuoto.
4. Rifiuto diagnostico additivo: conservare TOOL_ARGUMENTS_REJECTED e messaggio
   originale, aggiungendo motivo preciso, tool, nome/percorso del parametro,
   vincolo violato e limite effettivo nei canali autorizzati. Non loggare body,
   query, codice o PII indiscriminatamente; valore respinto solo se sicuro.
   Riutilizzare aup.exception1.0/CR-37 con cause/correlazioni e disponibilita
   stack onesta, non un nuovo sistema di errori o autenticazione.
5. Contratti pubblicati retrocompatibili: mantenere comportamento nativo/stdio,
   campi legacy, codici e policy di default. Owner registra esplicitamente
   l'incoerenza e la risoluzione contrattuale; se restringere uno schema pubblico
   richiede nuova versione/profilo, consegnare migrazione e versione dichiarate,
   non un breaking change silenzioso. Non ampliare la policy HTTP per far
   semplicemente passare valori che prima erano vietati.
6. Test con schema ottenuto da tools/list e runtime reale: default espliciti,
   omissione, minimo/massimo/confini fuori range,0 e override config. Provare
   che schema/default e validazione siano coerenti, includendo configurazioni
   che rendono il default nativo maggiore del ceiling HTTP.
7. Test di regressione input numerico source_id, null non ammessi, campi extra,
   path/progetto non selezionabili, sola lettura, tenant/auth invariati;
   SDK standard e passaggio ASMS/Agent OS, Windows e Linux quando supportati.

## Consegna e confini

Fornire baseline sorgente, versione/build/SHA256, schema prima/dopo, contratto
di compatibilita/migrazione, comandi e risultati reali e diagnostico di esempio
redatto. DELIVERED solo con evidenza; VERIFIED dopo pin e collaudo consumer.
ASMS rileggera gli schemi in una nuova sessione dopo consegna verificata:
nessun preset da ricreare salvo diversa necessita dimostrata dal contratto.

La presente run modifica solo documenti: registro append-only ASMS e copia CR
owner. Nessun codice, processo, configurazione, binding, database, auth, UI o
migration modificati. Recupero delle attestazioni7→19/8→20 resta distinto e
non dichiarato completato. Nessun test/build produttivo richiesto per redazione;
verifica diff, review indipendente e checksum copie. Nessun commit/push.
Review: PENDING_ARCHITECT_REVIEW. Stato tecnico PROPOSED.

Esito documentale indipendente architect_0820: RELEASE APPROVED, nessun finding.
Consentito il solo deposito della CR; nessuna modifica produttiva o aumento
dei limiti autorizzati. Stato tecnico ancora PROPOSED.

# CR-37 — Diagnostici CBM preservati tra sistemi eterogenei

## 0810 — Richiesta e responsabilita

Data: 28 ottobre 2026. Richiedente: AUP-SMS. Owner destinatario: CBM,
CodebaseMemory/codebase-memory-mcp-fork. Stato: PROPOSED.
Classificazione: contract-change / hardening.
Richiesta umana: nuova CR CBM, nessuna eccezione nascosta; usare il protocollo
distribuito gia definito, senza nuovi sistemi diagnostici o di autenticazione.
Prosegue CR-35 con nuova evidenza riproducibile; non riscrive o chiude CR-35.
CR-36 riguarda invece shadow/generazione e rimane una richiesta distinta.

Blocca la verifica completa del diagnostico CBM -> ASMS -> Agent OS e il
collaudo end-to-end della run Team. Non e la correzione del binding scaduto:
SOURCE_NOT_ACTIVE deve continuare a essere un errore, non diventare successo.

CBM gia emette codice/messaggio/correlazione e cause nel formato legacy:
non e dimostrato che CBM abbia nascosto il body in questo incidente.
La perdita nel percorso Agent OS e osservata, ma il punto responsabile tra
relay, SDK e client non e ancora attribuito. Questa CR richiede compatibilita
del produttore e prova interoperabile; eventuali correzioni del client Agent
OS appartengono al suo owner, non a una patch locale CBM o ASMS sostitutiva.

## Evidenza e baseline

Binario configurato/verificato: build/cr36-final/codebase-memory-mcp.exe,
0.12.0-asms.cr36.1, SHA256
f8924842dec0189c79671916ec666f7d0e243e14d1d0a1142ac6a4a212e154cf.
Baseline sorgente dichiarata owner:
a7211dbb33dfc5d8fca77d03507d1135581ae8e1.

28 ottobre, letture reali tenant1/progetto3:
- health nativa autenticata: state ACTIVE, sources=[];
- get_graph_schema con source_id="7": HTTP400, JSON-RPC error.code=-32000;
- error.data.code=SOURCE_NOT_ACTIVE;
- messaggio originale: Exact source binding is absent, revoked, expired or unverifiable;
- request_id e correlation_id nativi:
  3502a750d893afd29b76e8c82412adbdda968abc8b88b5082d631b78fb65bc46;
- error.data.details=[] e causes=[]: nessuno stack C esistente viene presunto.

Il consumer ASMS ProjectCbmDataClient ha restituito il codice e messaggio
originali, conservando JSON-RPC/data e correlazioni nel proprio errore.
Il diagnostico Agent OS fornito dall'utente, catturato2026-10-28T21:59:44Z,
ha diagnostic_id815611da-08e5-43ba-9105-66955e271b37,
request_id req_3302841f-e422-46f8-9602-4bf7c6387fd0,
correlation_id req_6605a5ee-5213-4d78-b78f-efef52048855.
Contiene HTTPStatusError400 e stack Python, ma non il body CBM:
completeness.complete=false, limitations=[upstream_diagnostic_unavailable].
Il nuovo probe locale dimostra il rifiuto attuale; non prova che il body
della precedente richiesta Agent OS fosse identico.

Sorgenti owner ispezionate in sola lettura:
- integrations/aup-sms/mcp-http/src/mcp_http.c:389-418, error_json conserva
  l'envelope legacy, sqlite_causes e verification_failure; nei JSON-RPC lo
  inserisce in error.data;
- stesso file:1757-1759, SOURCE_NOT_ACTIVE quando il binding non e utilizzabile;
- stesso file:738, verification_stage aggiunto per il controllo artefatti.

Comandi riproducibili dalla root CBM:
```powershell
rg -n 'error_json|SOURCE_NOT_ACTIVE|verification_stage' integrations/aup-sms/mcp-http/src/mcp_http.c
rg -n 'aup.exception' integrations/aup-sms/mcp-http/src
```
Esito osservato: emettitore e rifiuto presenti; nessuna occorrenza di
aup.exception nel sorgente del trasporto ispezionato. Non significa assenza
di qualsiasi supporto diagnostico in tutto il repository.

## Riproduzione del rifiuto e della propagazione

Usare ambiente di test isolato, credenziali tecniche valide mai stampate,
trasporto avviato con configurazione qualificata ma nessun binding per7.
Non revocare binding reali e non usare la produzione per creare il difetto.

1. Health con control bearer: ACTIVE, sorgente7 assente.
2. Sul data endpoint /integrations/aup-sms/v1/projects/t1-p3/mcp, data bearer,
   Content-Type application/json, Accept application/json, text/event-stream,
   MCP-Protocol-Version2025-06-18: initialize, notifications/initialized,
   tools/list e poi la chiamata seguente (rispettare eventuale sessione SDK):
```json
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_graph_schema","arguments":{"source_id":"7"}}}
```
3. Conservare status, content type e body originali del rifiuto. Atteso
   SOURCE_NOT_ACTIVE con messaggio/correlazione disponibili, non solo "400".
4. Ripetere stessa condizione nel client MCP SDK standard, poi nel relay ASMS
   e nel consumer Agent OS. Confrontare body ricevuto e diagnostico risultante
   a ciascun confine. Redigere matrice dei campi conservati/perduti per hop.

Il probe consumer usato e ProjectCbmDataClient.verify con endpoint interno,
credenziali lette in memoria dal deployment, sourceId7 e tool richiesto
get_graph_schema. Rapporto ASMS docs/116-0800-attesa-avvio-cbm.md conserva
l'esito; nessuna credenziale o corpo di run LLM e richiesto per riprodurre.

## Contratto richiesto e criteri di accettazione

1. Riusare aup.exception1.0 gia definito con CR23 Agent OS / CR-34 ASMS e
   CR-35 CBM. Riferimento canonico owner:
   C:/laragon/www/api2mcp-api/docs/change-requests/CR23-from-aupsms-team-run-root-cause-and-distributed-exceptions.md.
   Il consumer ASMS ha lo schema in packages/aup-modules/asms-core/contracts/distributed-exception/1.0/schema.json.
   Non inventare header, codec paralleli, auth o una piattaforma log nuova.
2. Pubblicare un'integrazione additiva compatibile col contratto MCP/JSON-RPC
   e HTTP esistente. Owner definisce e documenta la collocazione supportata del
   diagnostico e la sua lettura da parte dell'SDK/consumer. Nessuna modifica
   silenziosa a status, campi legacy, significato, default o input obbligatori.
   Un'eventuale variazione breaking richiede contratto versionato distinto.
3. Conservare senza rinomina/sostituzione codice e messaggio nativi, origine,
   cause disponibili incluse OS/SQLite, verification_stage, identificatori,
   correlazioni e contesto autorizzato. Non fermarsi a HTTPStatusError o a un
   messaggio generico; niente catch vuoti o fallimenti convertiti in successo.
4. In C puo esistere un errore restituito, non un oggetto eccezione o stack:
   rappresentarne fedelmente la natura e dichiarare lo stack unavailable
   secondo lo schema. Non fabbricare classi, frame, cause o complete=true.
5. Quando manca il diagnostico strutturato upstream, i consumer devono
   preservare il body legacy autorizzato, status/content type e correlazioni,
   con incompletezza esplicita; non scartarlo. Coordinare questa parte con
   Agent OS se la perdita si trova nel client. Limitare e dichiarare eventuali
   troncamenti, conservando un riferimento alla trace autorizzata completa.
6. Solo token, segreti e PII classificate possono essere redatti; i diagnostici
   sensibili/stack restano nei canali autorizzati. Isolamento tenant/source,
   autenticazione, firma, integrita e policy di trasporto non si indeboliscono.
7. Riutilizzare strutture diagnostiche gia presenti. Intervento semplice,
   nessuna riscrittura dell'architettura o copia del runtime Agent OS in CBM.

## Test richiesti e consegna

- errore SOURCE_NOT_ACTIVE completo nel percorso SDK standard e relayed;
- errore artefatto con verification_stage e cause OS/SQLite conservate;
- errore del worker annidato e causa secondaria, senza perdita dell'originale;
- errore trasporto senza risposta e risposta legacy/non JSON, incompletezza
  esplicita, mai un errore inventato o successo;
- confronto code/message/id/correlations/cause/stack availability a ogni hop;
- contratto aup.exception validato contro schema canonico, non una copia nuova;
- compatibilita client legacy/status/JSON-RPC e default invariati, percorsi
  Windows e Linux, redazione segreti e tenant/source isolation;
- il test distingue il produttore che emette il body dal client che lo perde:
  qualunque dipendenza Agent OS va dichiarata, non mascherata da patch CBM.

Consegna con commit sorgente, build/versione/SHA256, fixture sicure, comandi ed
esiti reali, evidenza del passaggio SDK/ASMS/Agent OS e limiti residui.
Stato DELIVERED solo con evidenze; ASMS marca VERIFIED dopo il collaudo sulla
baseline fissata. La run Team storica non e dichiarata risolta da questa CR.

## Ambito e verifica della presente richiesta

Solo documentazione: registro append-only ASMS e copia di consegna nella
cartella change-requests CBM. Nessun codice owner o consumer, API, migration,
metadata, autenticazione, processo, binding o credenziale modificato.
Nessuna build o test produttivo richiesto per la sola redazione; controllare
diff, identita delle copie e review indipendente. Rollback: aggiunta di nota
di supersessione con nuovo ID, non riscrittura della richiesta storica.
Nessun commit/push. Stato revisione: PENDING_ARCHITECT_REVIEW.

### Esito revisione documentale — 28 ottobre 2026

Release Gate indipendente architect_0800: APPROVED, nessun finding.
Consentita la sola consegna documentale. Stato tecnico CR ancora PROPOSED;
nessuna implementazione, prova end-to-end o risoluzione del binding dichiarata.

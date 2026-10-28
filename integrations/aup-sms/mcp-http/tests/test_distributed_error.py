"""Validate the native adapter against the external canonical schema, never a copy.
python test_distributed_error.py DRIVER CANONICAL_SCHEMA
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import unittest

from jsonschema import Draft202012Validator, FormatChecker

DRIVER = str(Path(sys.argv.pop(1)).resolve())
SCHEMA = json.loads(Path(sys.argv.pop(1)).read_text(encoding='utf-8'))
VALIDATOR = Draft202012Validator(SCHEMA, format_checker=FormatChecker())

def error(code='SOURCE_NOT_ACTIVE', message='Exact source binding is absent, revoked, expired or unverifiable', causes=None):
    return {'contract':'asms-cbm-http/v1','type':'error','code':code,'message':message,
            'request_id':'native-request','correlation_id':'native-correlation','details':{},'causes':causes or []}

class DistributedError(unittest.TestCase):
    def render(self, response, **options):
        process = subprocess.run([DRIVER], input=json.dumps({'response':response, **options}),
                                 capture_output=True, text=True, encoding='utf-8', check=True)
        result = json.loads(process.stdout)
        target = result.get('error',{}).get('data') or result.get('result',{}).get('structuredContent') or result
        diagnostic = target.get('diagnostic')
        if diagnostic is not None:
            VALIDATOR.validate(diagnostic)
            ids = []
            def inspect(node):
                ids.append(node['id'])
                self.assertEqual(node['stack']['availability'], 'unavailable')
                self.assertIsNone(node['stack']['text'])
                for child in node['children']: inspect(child)
            inspect(diagnostic['primary'])
            for item in diagnostic['secondary']: inspect(item['exception'])
            self.assertEqual(len(ids), len(set(ids)))
            self.assertFalse(diagnostic['completeness']['complete'])
        return result, diagnostic

    def test_source_not_active_keeps_legacy_and_native_identity(self):
        legacy = error()
        result, diagnostic = self.render({'jsonrpc':'2.0','id':3,'error':{'code':-32000,'message':legacy['message'],'data':legacy}})
        preserved = copy.deepcopy(result['error']['data']); preserved.pop('diagnostic')
        self.assertEqual(preserved, legacy)
        self.assertEqual(diagnostic['primary']['code'], legacy['code'])
        self.assertEqual(diagnostic['primary']['message'], legacy['message'])
        self.assertEqual(diagnostic['origin']['language'], 'c')
        self.assertEqual(diagnostic['origin']['request_id'], 'native-request')
        self.assertEqual(diagnostic['origin']['correlation_id'], 'native-correlation')
        self.assertEqual(diagnostic['origin']['identifiers']['jsonrpc_id'], 3)
        self.assertEqual(diagnostic['origin']['identifiers']['jsonrpc_error_code'], -32000)
        self.assertEqual(diagnostic['completeness']['limitations'], ['native_stack_unavailable'])

    def test_flat_control_failure_and_original_sqlite_fields(self):
        cause = {'jsonrpc':'2.0','id':0,'result':{'isError':True,'structuredContent':{
            'code':'ARTIFACT_IDENTITY_OR_INTEGRITY_FAILED','message':'original native message',
            'sqlite_code':101,'sqlite_message':'no more rows available','verification_stage':'native_generation',
            'errno':0,'os_message':''}}}
        result, diagnostic = self.render(error('ATTESTATION_UNVERIFIABLE', causes=[cause]), status=409)
        native = diagnostic['primary']['children'][0]
        self.assertEqual(native['message'], 'original native message')
        for field, value in [('sqlite_code',101),('sqlite_message','no more rows available'),('verification_stage','native_generation')]:
            self.assertEqual(native['details'][field], value)
        self.assertEqual(result['causes'][0], cause)

    def test_native_tool_error_is_not_renamed_by_audit_wrapper(self):
        native = {'isError':True,'content':[{'type':'text','text':'original cypher parser failure'}],
                  '_meta':{'code':'NATIVE_TOOL_FAILED','native':{'correlation_id':'native-tool-id'},
                           'causes':[error('AUDIT_POSTFLIGHT_FAILED', 'original audit failure')]}}
        result, diagnostic = self.render({'jsonrpc':'2.0','id':4,'result':native}, status=200)
        self.assertTrue(result['result']['isError'])
        self.assertIsNone(diagnostic['primary']['code'])
        self.assertEqual(diagnostic['primary']['message'], 'original cypher parser failure')
        self.assertEqual(diagnostic['primary']['details']['native_metadata']['correlation_id'], 'native-tool-id')
        self.assertEqual(diagnostic['secondary'][0]['exception']['code'], 'AUDIT_POSTFLIGHT_FAILED')
        self.assertEqual(diagnostic['secondary'][0]['exception']['message'], 'original audit failure')

    def test_worker_cause_retains_original_failure_and_flags(self):
        failure = {'type':'native_subprocess','outcome':2,'exit_code':-1,'term_signal':11,
                   'stderr':'original worker error','tree_quiesced':True,'captures_retained':False}
        _, diagnostic = self.render(error('WORKER_FAILED', causes=[failure]))
        self.assertEqual(diagnostic['primary']['children'][0]['type'], 'native_subprocess')
        for field in failure.keys() - {'type'}:
            self.assertEqual(diagnostic['primary']['children'][0]['details'][field], failure[field])

    def test_tool_structured_content_and_text_are_preserved(self):
        native = {'isError': True, 'content': [{'type': 'text', 'text': 'original failure'}],
                  'structuredContent': {'code': 'REAL_ERROR', 'message': 'original failure', 'sqlite_code': 101}}
        result, diagnostic = self.render({'jsonrpc': '2.0', 'id': 9, 'result': native}, status=200)
        preserved = copy.deepcopy(result['result'])
        preserved['structuredContent'].pop('diagnostic')
        self.assertEqual(preserved, native)
        self.assertEqual(diagnostic['primary']['code'], 'REAL_ERROR')
        self.assertEqual(diagnostic['primary']['details']['sqlite_code'], 101)

    def test_oversize_wrapper_keeps_original_error_in_diagnostic(self):
        original = error('ACTUAL_NATIVE_FAILURE', 'original failure')
        result, diagnostic = self.render(error('RESPONSE_LIMIT'), original=original, status=413, limit=4096)
        self.assertEqual(result['code'], 'RESPONSE_LIMIT')
        self.assertEqual(diagnostic['primary']['code'], 'ACTUAL_NATIVE_FAILURE')
        self.assertIn('response_limit_exceeded', diagnostic['completeness']['limitations'])
        self.assertIsNone(diagnostic['completeness']['diagnostic_reference'])

    def test_success_before_size_limit_is_not_fabricated_as_upstream_error(self):
        _, diagnostic = self.render(error('RESPONSE_LIMIT'), original={'jsonrpc':'2.0','result':{'isError':False}}, status=413)
        self.assertEqual(diagnostic['primary']['code'], 'RESPONSE_LIMIT')

    def test_causes_and_long_unicode_messages_have_explicit_limits(self):
        _, diagnostic = self.render(error(message='é🙂'*2000, causes=[error('CHILD') for _ in range(30)]))
        self.assertIn('native_message_truncated', diagnostic['completeness']['limitations'])
        self.assertIn('native_causes_truncated', diagnostic['completeness']['limitations'])

    def test_success_does_not_acquire_error_metadata(self):
        success = {'jsonrpc':'2.0','id':1,'result':{'isError':False,'content':[]}}
        result, diagnostic = self.render(success)
        self.assertEqual(result, success)
        self.assertIsNone(diagnostic)

    def test_budget_never_replaces_a_fitting_legacy_error(self):
        legacy = error()
        result, diagnostic = self.render(legacy, limit=len(json.dumps(legacy))+20)
        self.assertEqual(result, legacy)
        self.assertIsNone(diagnostic)

if __name__ == '__main__': unittest.main(verbosity=2)

"""Opt-in read-only target queries at an existing NS2 transport rendezvous.
No interactive pause, arbitrary Python, or NVMe control actions are accepted.
A query extends one NS2 call, so do not run it inside a timing comparison.
"""
import json
import os
import time
from pathlib import Path

_query_base = hv.nwoas_nvme_link_handler
_query_root = Path(os.environ['NWOAS_LINK_DIR'])
_query_request = _query_root / 'target-query.json'
_query_result = _query_root / 'target-query-result.json'
_query_last_poll = 0.0
_query_last_id = None


def _query_validate(value):
    if not isinstance(value, dict) or set(value) != {'id', 'actions'}:
        raise ValueError('expected id and actions only')
    ident = value['id']
    actions = value['actions']
    if not isinstance(ident, str) or not 1 <= len(ident) <= 64:
        raise ValueError('invalid query id')
    if not isinstance(actions, list) or not 1 <= len(actions) <= 8:
        raise ValueError('invalid action list')
    if any(type(a) is not int or a not in (3,4,5,6,7,8,9,10,17,18,19,20,21,22) for a in actions):
        raise ValueError('only read-only actions 3..10 and 17..22 are allowed')
    return ident, actions


def _query_poll():
    global _query_last_poll, _query_last_id
    now = time.monotonic()
    if now - _query_last_poll < 1.0:
        return
    _query_last_poll = now
    if not _query_request.exists():
        return
    ident = None
    started = time.time()
    try:
        with _query_request.open('r') as stream:
            body = stream.read(4097)
        if len(body) > 4096:
            raise ValueError('request too large')
        ident, actions = _query_validate(json.loads(body))
        if ident == _query_last_id:
            return
        _query_last_id = ident
        capability = p.nwoas_nvme_fastpath(8)
        if capability >> 32 != 0x53313630:
            raise ValueError('target mask-query capability mismatch')
        values = {str(a): p.nwoas_nvme_fastpath(a) for a in actions}
        result = {'id': ident, 'started': started, 'finished': time.time(), 'values': values}
    except (ValueError, OSError) as error:
        result = {'id': ident, 'started': started, 'finished': time.time(),
                  'error': str(error)}
    temp = _query_result.with_suffix('.tmp')
    temp.write_text(json.dumps(result, indent=2) + '\n')
    temp.replace(_query_result)


def _query_link_handle(addr):
    result = _query_base(addr)
    if result:
        try:
            _query_poll()
        except OSError as error:
            hv.log('[S163] target-query result write failed: ' + str(error))
    return result


if os.environ.get('NWOAS_TARGET_QUERY') == '1':
    hv.nwoas_nvme_link_handler = _query_link_handle
    hv.log('[S163] opt-in read-only target queries enabled; timing probes are on demand only')

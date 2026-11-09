#!/usr/bin/env python3
"""Report export coverage without equating a symbol body with full behavior."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT/'dist/ce-demo'
STUBS = {'InvertRect', 'PropertySheetW', 'IsCommandBarMessage',
         'CommandBands_AddBands', 'CommandBands_GetCommandBar',
         'CommandBands_GetRestoreInformation', 'ListView_SetItemSpacing'}
PARTIAL = {'CreateToolbar', 'CreateToolbarEx', 'CommandBar_InsertMenubar',
           'CommandBar_InsertMenubarEx', 'CommandBar_DrawMenuBar',
           'CommandBar_AddBitmap', 'CommandBands_Create', 'CommandBands_Show',
           'CommandBands_AddAdornments'}
ANONYMOUS = {57:'DSA_Sort',58:'DSA_SetItemRange',59:'DPA_Search',
             60:'DPA_DeletePtr',61:'DPA_DeleteAllPtrs',62:'DPA_GetPtrIndex'}
def main():
    manifest = json.loads((HERE/'abi/commctrl.dll.json').read_text())
    entries = []
    for export in manifest['exports']:
        name = export['name'] or ANONYMOUS.get(export['ordinal'])
        state = ('unresolved-private-stub' if name is None else
                 'stub' if name in STUBS else 'partial-model' if name in PARTIAL else
                 'container-implementation' if name.startswith(('DPA_', 'DSA_')) or name=='Str_SetPtrW' else
                 'registration-model' if name.startswith('Init') or name.endswith('_Init') else
                 'basic-rendering' if name=='DrawStatusTextW' else 'state-model')
        entries.append({'ordinal':export['ordinal'], 'export_name':export['name'],
                        'identified_name':name, 'status':state,
                        'reference_rva':export['rva']})
    control = json.loads((HERE/'abi/control.exe.json').read_text())
    profile = json.loads((OUT/'COREDLL.DLL.profile.json').read_text())
    unsupported = {e['ordinal'] for e in profile['unsupported']}
    imports = [s for mod in control['imports'] if mod['module'].lower()=='coredll.dll'
               for s in mod['symbols']]
    missing = [s for s in imports if s['ordinal'] in unsupported]
    result = {'reference_sha256':manifest['sha256'], 'complete_behavioral_port':False,
              'reference_decompilation_functions':1414,
              'exports':entries, 'control_coredll_imports':len(imports),
              'control_untyped_imports':missing,
              'known_limits':['MSVC scalar/home-space exports and callbacks tested; floating-point, aggregates and unclassified APIs unsupported',
                 'property-sheet modal engine, menu resources/UI and icon pixel copies incomplete',
                 'tooltip/rebar/date-control behavior incomplete',
                 'notifications, subclass callbacks and all control messages not fully implemented',
                 'reference decompilation is not compilable port source']}
    assert not missing, missing
    (OUT/'port-status.json').write_text(json.dumps(result, indent=2)+'\n')
    print(f'COMMCTRL: {len(entries)} exact export slots; {len(ANONYMOUS)} anonymous container exports identified')
    print(f'Control: all {len(imports)} COREDLL imports have typed bodies (some deliberate stubs)')
if __name__=='__main__':
    main()

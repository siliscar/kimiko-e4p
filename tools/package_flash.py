#!/usr/bin/env python3
"""中文：从同一次编译生成基础包、TF 升级包与 OTA 清单。/ English: Stage base, TF and OTA from one build.

用法 / Usage: package_flash.py <build> [<out>] [<notes.txt>]
第三个参数是可选的更新说明文本，按行写；缺省时使用一条指向官网更新日志的通用说明。
The third argument is optional release-note text, one line per item; without it a generic pointer
to the official changelog is used.
"""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib, json, shutil, sys
from verify_flash_bundle import EXPECTED, LAYOUT, PAGES_ORIGIN, check
ROOT=Path(__file__).resolve().parents[1]
def package(build: Path, output: Path, notes: str | None = None):
    config=(build/'config/sdkconfig.h').read_text()
    assert '#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1' in config, 'rollback bootloader required'
    data=(build/'Read_Pico.bin').read_bytes()
    assert data[0]==0xe9 and int.from_bytes(data[32:36],'little')==0xabcd5432
    version=data[48:80].split(b'\0')[0].decode('ascii')
    assert b'PICO_HTTPS_OTA_V1' in data, 'base package lacks online OTA support'
    upgrade_name=f'Pico-update-{version}.bin'
    # 本地同版本测试只能打到构建目录，已发布文件的下载内容必须固定。
    # Same-version local tests belong in build directories; published downloads stay immutable.
    published=ROOT/'flash'/upgrade_name
    if output.resolve()==(ROOT/'flash').resolve() and published.is_file():
        assert published.read_bytes()==data, (
            'published version already exists with different bytes; '
            'stage local tests in build/, or increment the version for an authorized release')
    template=ROOT/'flash/manifest.json'
    if template.is_file():
        manifest=json.loads(template.read_text())
    else:
        # 没有站点模板时生成最小可刷清单，便于只打包固件并交给外部平台。
        # Without a site template, synthesize a minimal flashable manifest for bundle-only staging.
        manifest={'name':'kimiko','new_install_prompt_erase':True,
            'builds':[{'chipFamily':LAYOUT['chip_family'],'parts':[
                {'path':name,'offset':EXPECTED[name][0]}
                for name in ('bootloader.bin','partitions.bin','ota_data_initial.bin','firmware.bin')]}]}
    manifest['version']=version
    manifest['name']='kimiko'
    manifest['updated_at']=datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(timespec='seconds')
    manifest['pico_ota']={'layout':LAYOUT['layout'],'base_version':version,
        'minimum_base_version':LAYOUT['minimum_base_version'],
        'online_ota_base_version':LAYOUT['online_ota_base_version'],'rollback':True}
    output.mkdir(parents=True,exist_ok=True)
    parts={'firmware.bin':build/'Read_Pico.bin','bootloader.bin':build/'bootloader/bootloader.bin',
           'partitions.bin':build/'partition_table/partition-table.bin','ota_data_initial.bin':build/'ota_data_initial.bin'}
    for name,path in parts.items():shutil.copy2(path,output/name)
    shutil.copy2(build/'Read_Pico.bin',output/upgrade_name)
    shutil.copy2(build/'Read_Pico.bin',output/'Pico-update.bin')
    notes=notes or f'版本 {version}；更新内容见官网更新日志'
    # 固件清单最多收 383 字节说明，超了整份清单会被设备拒收。
    # The device accepts at most 383 UTF-8 bytes of notes; a longer string rejects the whole manifest.
    assert 0<len(notes.encode('utf-8'))<384, 'release notes must fit 383 UTF-8 bytes'
    release={'schema':1,'version':version,'project':LAYOUT['project'],'board':LAYOUT['board'],
        'layout':LAYOUT['layout'],'minimum_base_version':LAYOUT['minimum_base_version'],
        'url':PAGES_ORIGIN+upgrade_name,
        'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),'notes':notes}
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    (output/'update.json').write_text(json.dumps(release,ensure_ascii=False,indent=2)+'\n')
    check(output)
if __name__=='__main__':
    notes=Path(sys.argv[3]).read_text(encoding='utf-8').strip() if len(sys.argv)>3 else None
    package(Path(sys.argv[1]),Path(sys.argv[2]) if len(sys.argv)>2 else ROOT/'flash',notes)

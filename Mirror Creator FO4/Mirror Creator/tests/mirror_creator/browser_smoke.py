"""Exercise the real Fallout picker, Shift-click, save/reopen and server shutdown."""
from pathlib import Path
import argparse
import json
import sys
import threading

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/mirror_creator'))
sys.path.insert(0,str(ROOT/'tests/mirror_creator'))
from mirror_creator import CreatorServer
from mirror_creator_core import load_nif
from test_creator import fixture, separated_fixture, broken_fixture


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    from playwright.sync_api import sync_playwright, expect
    errors=[];checks=[];external=[]
    server=CreatorServer(0);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
        with sync_playwright() as p:
            browser=p.chromium.launch(executable_path='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
                                      headless=True,args=['--use-angle=swiftshader','--enable-unsafe-swiftshader'])
            page=browser.new_page(viewport={'width':1440,'height':1000},accept_downloads=True)
            page.on('pageerror',lambda error:errors.append(str(error)))
            page.on('request',lambda request:external.append(request.url) if not request.url.startswith(server.origin) else None)
            page.goto(server.origin,wait_until='networkidle')
            expect(page).to_have_title('Mirror Creator · Fallout 4')
            expect(page.locator('#save')).to_be_disabled()
            def idle():expect(page.locator('#viewport')).to_have_attribute('aria-busy','false')
            def upload(raw,name='source.nif'):
                with page.expect_response('**/api/import?*') as r:
                    page.locator('#nif-input').set_input_files({'name':name,'mimeType':'application/octet-stream','buffer':raw})
                idle()
                page.screenshot(path=str(args.output/'latest-import.png'))
                print('IMPORT',name,r.value.status,'message:',page.locator('#message').text_content(),'errors:',errors,flush=True)
                return r.value
            def click(x=.5,y=.5,shift=False):
                box=page.locator('#canvas').bounding_box()
                if shift:page.keyboard.down('Shift')
                try:
                    with page.expect_response('**/api/select') as r:
                        page.mouse.click(box['x']+box['width']*x,box['y']+box['height']*y)
                finally:
                    if shift:page.keyboard.up('Shift')
                idle();assert r.value.ok,r.value.text();return r.value.json()
            def save(name):
                with page.expect_download() as download:page.locator('#save').click()
                path=args.output/name;download.value.save_as(path);idle()
                doc=load_nif(path.read_bytes(),path.name)
                assert sum(mesh.name=='MOFReflectiveSurface:0' for mesh in doc.meshes)==1
                return path
            def passed(label):checks.append(label);print('PASS',label,flush=True)
            assert upload(fixture()).ok
            click();expect(page.locator('.ck-part')).to_have_text('Part: MOFCKPane · NIF block 1')
            first=save('flat-browser.nif');passed('Load, real 3D click and download produce a Fallout prepared NIF')
            assert upload(first.read_bytes()).ok
            click();save('reexport-browser.nif');passed('Prepared NIF can be reopened and exported again')
            raw,other=separated_fixture();assert upload(raw,'two-panes.nif').ok
            click(.335);click(.665,shift=True)
            expect(page.locator('.ck-part')).to_have_count(2)
            expect(page.locator('#ck-note')).to_contain_text('one whole part')
            combined=save('combined-browser.nif')
            assert len(next(m for m in load_nif(combined.read_bytes()).meshes if m.name=='MOFReflectiveSurface:0').triangles)==4
            page.screenshot(path=str(args.output/'combined.png'))
            click(.335,shift=True);expect(page.locator('.ck-part')).to_have_count(1)
            passed('Shift-click combines separate parts, preserves gap and removes selected areas')
            assert upload(broken_fixture(),'broken.nif').ok
            click(.38);expect(page.locator('#ck-note')).to_contain_text('whole part')
            click(.62,shift=True);save('shards-browser.nif')
            passed('Disconnected areas within one mesh part remain selectable')
            assert not upload(b'bad','invalid.nif').ok
            expect(page.locator('#message')).to_be_visible()
            expect(page.locator('#save')).to_be_enabled()
            passed('Invalid import displays an error and preserves current selection')
            assert upload(fixture('Hole'),'hole.nif').ok
            click(.38);save('hole-browser.nif');page.screenshot(path=str(args.output/'hole.png'))
            passed('A pane with a hole previews and exports with its empty centre')
            assert not external,external
            assert not errors,errors
            assert page.request.get(server.origin+'/api/health',headers={'Origin':'https://example.com'}).status==403
            passed('Preview loads locally and rejects foreign-origin API access')
            page.locator('#close').click()
            expect(page.locator('#message')).to_contain_text('has closed')
            worker.join(timeout=3);assert not worker.is_alive()
            passed('Close button stops the local server')
            browser.close()
    finally:
        if worker.is_alive():server.shutdown();worker.join(timeout=3)
        server.server_close()
    (args.output/'results.json').write_text(json.dumps(dict(checks=checks,errors=errors,externalRequests=external),indent=2))


if __name__=='__main__':main()

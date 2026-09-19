"""Exercise the minimal creator in Edge using optional, isolated Playwright."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'tests'))
from mirror_creator_core import load_nif
from test_mirror_creator import fixture
from test_mirror_creator_export import metadata
from mirror_creator_export import TRIANGLES_NAME, PROFILE_NAME


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://127.0.0.1:8765/')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--edge',default='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    from playwright.sync_api import sync_playwright,expect
    errors=[];external=[];checks=[]
    def passed(name):checks.append(name);print('PASS',name,flush=True)
    with sync_playwright() as playwright:
        browser=playwright.chromium.launch(executable_path=args.edge,headless=True,args=['--use-angle=swiftshader','--enable-unsafe-swiftshader'])
        page=browser.new_page(viewport={'width':1440,'height':1000},device_scale_factor=1,accept_downloads=True)
        page.set_default_timeout(15000)
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('request',lambda r:external.append(r.url) if not r.url.startswith(args.url) and not r.url.startswith(('blob:','data:')) else None)
        def idle():
            expect(page.locator('#viewport')).to_have_attribute('aria-busy','false')
        def quiet():
            expect(page.locator('#message')).to_be_hidden()
            assert ' '.join(page.locator('.toolbar').inner_text().split())=='Select NIF Save hold shift to select multiple parts'
        def upload(file):
            with page.expect_response('**/api/import?*'):
                page.locator('#nif-input').set_input_files(file)
            idle()
        def click_pane(x=.5,y=.5):
            box=page.locator('#canvas').bounding_box()
            with page.expect_response('**/api/select') as response:
                page.mouse.click(box['x']+box['width']*x,box['y']+box['height']*y)
            idle();expect(page.locator('#save')).to_be_enabled();quiet()
            return response.value.json()
        def save(prefix, expected_name=None):
            with page.expect_download() as download:page.locator('#save').click()
            name=download.value.suggested_filename
            assert name.lower().endswith('.nif')
            if expected_name is not None:
                assert name==expected_name
            saved=args.output/(prefix+name)
            download.value.save_as(str(saved));idle();quiet()
            return saved
        page.goto(args.url,wait_until='networkidle')
        expect(page.locator('#save')).to_be_disabled();expect(page.locator('#empty')).to_be_visible();quiet()
        assert page.locator('button:visible').count()==2
        page.screenshot(path=str(args.output/'empty.png'));passed('Wall type, Select NIF, disabled Save and the Shift-select hint are visible')

        hand=ROOT/'prototypes/hand-mirror/gilded-noble-round-filigree-v1/nifs/male-world.nif'
        original=hand.read_bytes()
        # Exercise the visible Select NIF control and the actual file chooser.
        with page.expect_file_chooser() as chooser:page.locator('#load').click()
        with page.expect_response('**/api/import?*'):chooser.value.set_files(str(hand))
        idle();expect(page.locator('#save')).to_be_disabled()
        surface=click_pane(y=.30)
        assert surface['triangleCount']==24,surface
        box=page.locator('#canvas').bounding_box()
        page.mouse.move(box['x']+20,box['y']+60)
        page.screenshot(path=str(args.output/'hand-selected.png'));passed('Select NIF and real pane click select all 24 round hand-pane faces')
        prepared=save('hand-', 'male-world.nif')
        _,_,tags=metadata(prepared.read_bytes())
        assert tags[PROFILE_NAME]==1 and len(tags[TRIANGLES_NAME])==24*6
        assert hand.read_bytes()==original
        passed('Save downloads the original filename as a wall-profile NIF without changing the input')

        before=page.locator('#canvas').screenshot()
        page.mouse.move(box['x']+box['width']*.5,box['y']+box['height']*.5)
        page.mouse.wheel(0,-200);page.wait_for_timeout(300)
        after=page.locator('#canvas').screenshot()
        assert hashlib.sha256(before).digest()!=hashlib.sha256(after).digest()
        page.mouse.down();page.mouse.move(box['x']+box['width']*.5+90,box['y']+box['height']*.5+25,steps=10);page.mouse.up()
        retained=save('after-orbit-')
        assert retained.read_bytes()==prepared.read_bytes()
        passed('Implicit wheel zoom and drag rotation preserve the exact selected pane')

        upload({'name':'bad.nif','mimeType':'application/octet-stream','buffer':b'not a NIF'})
        expect(page.locator('#message')).to_contain_text('could not be read')
        expect(page.locator('#save')).to_be_enabled()
        recovered=save('after-error-')
        assert recovered.read_bytes()==prepared.read_bytes()
        passed('Failed import shows a concise error and preserves the prior savable NIF')

        standing=ROOT/'dist/Data/meshes/mirrors_of_skyrim/mirror01.nif'
        upload(str(standing));expect(page.locator('#save')).to_be_disabled()
        surface=click_pane()
        assert surface['triangleCount']==2,surface
        prepared=save('wall-', 'mirror01.nif')
        _,_,tags=metadata(prepared.read_bytes())
        assert tags[PROFILE_NAME]==1 and len(tags[TRIANGLES_NAME])==12
        page.mouse.move(20,200);page.screenshot(path=str(args.output/'wall-selected.png'))
        passed('Standing-mirror selection saves the rectangular pane with the world profile')

        flat=fixture([(0,-2,-3),(0,2,-3),(0,2,3),(0,-2,3)],[(0,1,2),(0,2,3)])
        upload({'name':'two-sided-pane.nif','mimeType':'application/octet-stream','buffer':flat})
        front=click_pane();assert not front['flip'],front
        box=page.locator('#canvas').bounding_box()
        x=box['x']+box['width']*.5;y=box['y']+box['height']*.5
        # OrbitControls maps half the canvas height of horizontal drag to pi.
        page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+box['height']/2,y,steps=25);page.mouse.up()
        back=click_pane()
        assert back['flip'] and back['normal']==[-v for v in front['normal']],back
        prepared=save('back-')
        _,_,tags=metadata(prepared.read_bytes())
        assert tags[PROFILE_NAME]==1
        exported=load_nif(prepared.read_bytes()).meshes[0]
        assert exported.triangles==[(0,2,1),(0,3,2)],exported.triangles
        passed('Clicking the opposite side automatically reverses reflective facing and exported winding')

        upload(str(prepared));expect(page.locator('#save')).to_be_disabled()
        again=click_pane();assert again['triangleCount']==2
        saved_again=save('reopened-')
        _,_,tags=metadata(saved_again.read_bytes())
        assert len(tags[TRIANGLES_NAME])==12
        passed('A saved NIF can be reopened, selected and saved without a project workflow')

        combined=fixture([(0,-2,-3),(0,2,-3),(0,2,3),(0,-2,3),(2,2,3)],[(0,1,2),(0,2,3),(1,4,2)])
        upload({'name':'combined-frame-pane.nif','mimeType':'application/octet-stream','buffer':combined})
        selected=click_pane();assert selected['faces']==[0,1],selected
        page.mouse.move(20,200);page.screenshot(path=str(args.output/'combined-selected.png'))
        prepared=save('')
        loaded=load_nif(prepared.read_bytes())
        assert loaded.mesh(1).triangles==[(1,4,2)] and len(loaded.meshes)==2
        passed('A combined mesh needs only one click: Save retains its bent frame and separates the flat pane')

        for width,height in [(1000,800),(390,740)]:
            page.set_viewport_size({'width':width,'height':height})
            upload(str(standing));selected=click_pane()
            assert selected['triangleCount']==2
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth && document.documentElement.scrollHeight <= innerHeight')
            quiet();page.mouse.move(1,1);page.screenshot(path=str(args.output/('compact-'+str(width)+'.png')))
        passed('Compact desktop and narrow layouts retain working selection without scrolling or extra controls')
        assert not errors,errors
        assert not external,external
        passed('No browser script errors or runtime requests outside the local server')
        browser.close()
    result=dict(passed=len(checks),checks=checks,browser='Edge / Chromium',rendering='headless SwiftShader',errors=errors,externalRequests=external,gameLaunched=False)
    (args.output/'browser-tests.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()

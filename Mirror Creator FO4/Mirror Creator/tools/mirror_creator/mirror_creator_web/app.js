import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';

const $ = id => document.getElementById(id);
const state = { document: null, selection: null, busy: false, meshes: [], multiArea: false };
let renderer, scene, camera, controls, modelGroup, selectionGroup, hover;
let framePending = false, pointerDown = null, lastHover = null;
const raycaster = new THREE.Raycaster();

function message(text = '') {
  $('message').textContent = text;
  $('message').hidden = !text;
}

function showCKParts() {
  const panel = $('ck-selection'), parts = $('ck-parts'), note = $('ck-note');
  parts.replaceChildren(); note.textContent = ''; note.hidden = true;
  panel.hidden = !state.selection;
  if (!state.selection) return;
  const selected = new Map();
  for (const area of state.selection.areas || [state.selection]) {
    if (!selected.has(area.block)) selected.set(area.block, new Set());
    for (const face of area.faces) selected.get(area.block).add(face);
  }
  const sources = state.document.meshes.filter(mesh => selected.has(mesh.block));
  if (sources.some(mesh => !Number.isInteger(mesh.ckIndex) || mesh.ckIndex < 0 || typeof mesh.ckName !== 'string')) {
    note.textContent = 'Open the latest Mirror Creator address to see CK part names and indices.';
  } else {
    for (const mesh of sources) {
      const row = document.createElement('div'), name = document.createElement('code');
      row.className = 'ck-part';
      row.title = 'Part in the currently loaded NIF. The Fallout CK helper also lists this part name.';
      name.textContent = mesh.ckName || '(unnamed)';
      row.append('Part: ', name, ' · NIF block ' + mesh.block);
      parts.append(row);
    }
    if (sources.length > 1) {
      note.textContent = 'CK reflection supports one whole part. Use Save for these combined areas.';
    } else if (sources.some(mesh => selected.get(mesh.block).size < mesh.triangleCount)) {
      note.textContent = 'CK changes the whole part. Use Save for only the selected area.';
    }
  }
  note.hidden = !note.textContent;
}

function enabled() {
  $('save').disabled = !state.selection || state.busy;
  $('load').disabled = state.busy;
  $('close').disabled = state.busy;
  if (controls) controls.enabled = !state.busy;
  document.body.classList.toggle('processing', state.busy);
  $('viewport').setAttribute('aria-busy', String(state.busy));
}

async function operation(work) {
  if (state.busy) return;
  state.busy = true; enabled(); message();
  try { await work(); }
  catch (error) { message(error.message || 'The operation could not be completed.'); }
  finally { state.busy = false; enabled(); }
}

async function api(path, payload, binary = false) {
  const response = await fetch(path, {method: 'POST', headers: {'X-Mirror-Creator': '1', 'Content-Type': binary ? 'application/octet-stream' : 'application/json'}, body: binary ? payload : JSON.stringify(payload)});
  if (!response.ok) {
    const error = await response.json().catch(() => ({}));
    throw new Error(error.error || 'Could not complete the request (HTTP ' + response.status + ').');
  }
  return response;
}

function render() {
  if (framePending || !renderer) return;
  framePending = true;
  requestAnimationFrame(() => { framePending = false; renderer.render(scene, camera); });
}

function disposeGroup(group) {
  for (const child of [...group.children]) {
    group.remove(child);
    child.traverse(object => {
      object.geometry?.dispose();
      if (Array.isArray(object.material)) object.material.forEach(m => m.dispose());
      else object.material?.dispose();
    });
  }
}

function initializeViewer() {
  renderer = new THREE.WebGLRenderer({ canvas: $('canvas'), alpha: true, antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setClearColor(0, 0);
  scene = new THREE.Scene();
  camera = new THREE.PerspectiveCamera(36, 1, .01, 100000);
  camera.up.set(0, 0, 1);
  camera.position.set(80, -150, 100);
  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = false;
  controls.addEventListener('change', () => { hideHover(); render(); });
  modelGroup = new THREE.Group(); selectionGroup = new THREE.Group();
  scene.add(modelGroup, selectionGroup, new THREE.HemisphereLight(0xf2f4dd, 0x394938, 2.1));
  const key = new THREE.DirectionalLight(0xffedc7, 2.5); key.position.set(150, -200, 350); scene.add(key);
  const fill = new THREE.DirectionalLight(0xe5f6ff, 1.5); fill.position.set(-200, 150, 60); scene.add(fill);
  hover = new THREE.Mesh(new THREE.BufferGeometry(), new THREE.MeshBasicMaterial({color: 0xffdf95, opacity: .45, transparent: true, side: THREE.DoubleSide, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2}));
  hover.visible = false; scene.add(hover);
  new ResizeObserver(() => {
    const box = $('viewport').getBoundingClientRect();
    if (!box.width || !box.height) return;
    renderer.setSize(box.width, box.height, false);
    camera.aspect = box.width / box.height; camera.updateProjectionMatrix(); render();
  }).observe($('viewport'));
  $('canvas').addEventListener('pointerdown', e => { pointerDown = {x:e.clientX,y:e.clientY,button:e.button,additive:e.shiftKey}; });
  $('canvas').addEventListener('pointercancel', () => { pointerDown = null; });
  $('canvas').addEventListener('pointerup', e => {
    const start = pointerDown; pointerDown = null;
    if (start && start.button === 0 && Math.hypot(e.clientX-start.x, e.clientY-start.y) < 5 && !state.busy) {
      const hit = pick(e);
      if (hit) choose(hit, start.additive);
    }
  });
  $('canvas').addEventListener('pointermove', e => {
    if (e.buttons || state.busy) { hideHover(); return; }
    const hit = pick(e);
    if (!hit) { hideHover(); return; }
    const key = hit.object.userData.block + ':' + hit.faceIndex;
    if (key === lastHover) return;
    lastHover = key;
    const source = state.document.meshes.find(m => m.block === hit.object.userData.block);
    const positions = source.indices.slice(hit.faceIndex*3, hit.faceIndex*3+3).flatMap(v => source.positions.slice(v*3, v*3+3));
    hover.geometry.dispose(); hover.geometry = new THREE.BufferGeometry();
    hover.geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
    hover.visible = true; $('canvas').style.cursor = 'crosshair'; render();
  });
  $('canvas').addEventListener('pointerleave', hideHover);
  $('canvas').addEventListener('webglcontextlost', e => { e.preventDefault(); message('The 3D preview lost its graphics context. Reload this page and reopen your NIF.'); });
}

function hideHover() {
  if (hover?.visible) { hover.visible = false; render(); }
  lastHover = null; $('canvas').style.cursor = '';
}

function pick(e) {
  if (!state.document || !renderer) return null;
  const box = $('canvas').getBoundingClientRect();
  // Selection may arrive before the next requested render after fitting a model.
  camera.updateMatrixWorld();
  modelGroup.updateMatrixWorld(true);
  raycaster.setFromCamera(new THREE.Vector2((e.clientX-box.left)/box.width*2-1, -(e.clientY-box.top)/box.height*2+1), camera);
  // Pick the untouched source geometry even while its display is split into
  // a checker pane and the remaining frame. Face IDs must stay source IDs.
  return raycaster.intersectObjects(state.meshes.filter(m => !m.userData.hidden), false)[0] || null;
}

function initialDirection() {
  // Choose a useful viewing angle without selecting a reflective surface.
  let best = null, score = 0;
  for (const mesh of state.meshes) {
    if (!mesh.visible) continue;
    const size = mesh.geometry.boundingBox.getSize(new THREE.Vector3());
    const extents = [size.x, size.y, size.z].sort((a,b) => a-b);
    if (extents[0] <= extents[2]*.0001 && extents[1]*extents[2] > score) {
      const p = mesh.geometry.getAttribute('position'), index = mesh.geometry.index;
      for (let i=0; i<index.count; i+=3) {
        const a = new THREE.Vector3().fromBufferAttribute(p,index.getX(i));
        const b = new THREE.Vector3().fromBufferAttribute(p,index.getX(i+1));
        const c = new THREE.Vector3().fromBufferAttribute(p,index.getX(i+2));
        const n = b.sub(a).cross(c.sub(a));
        if (n.lengthSq() > 1e-12) { best=n.normalize(); score=extents[1]*extents[2]; break; }
      }
    }
  }
  if (best) return best;
  const size = new THREE.Box3().setFromObject(modelGroup).getSize(new THREE.Vector3());
  // Vanilla trays/plates contain the rim and centre in one thick shape. View
  // their broad XY face from above instead of opening the model edge-on.
  if (size.z < size.x && size.z < size.y) return new THREE.Vector3(0,0,1);
  return size.x < size.y && size.x < size.z ? new THREE.Vector3(1,0,0) : new THREE.Vector3(0,-1,0);
}

function fit() {
  const box = new THREE.Box3().setFromObject(modelGroup);
  const center = box.getCenter(new THREE.Vector3());
  const radius = Math.max(box.getSize(new THREE.Vector3()).length()/2, .01);
  const dir = initialDirection();
  camera.up.set(0,0,1); if (Math.abs(dir.z) > .97) camera.up.set(0,1,0);
  const angle = Math.min(THREE.MathUtils.degToRad(camera.fov/2), Math.atan(Math.tan(THREE.MathUtils.degToRad(camera.fov/2))*camera.aspect));
  const distance = radius / Math.sin(angle) * 1.08;
  camera.near = Math.max(radius/1000, .00001); camera.far = Math.max(radius*100, distance*10);
  camera.position.copy(center).addScaledVector(dir, distance);
  controls.target.copy(center); controls.minDistance = radius*.08; controls.maxDistance = radius*30;
  camera.updateProjectionMatrix(); controls.update(); render();
}

function loadResponse(response) {
  if (!renderer) throw new Error('The 3D preview is unavailable. Try Edge or Chrome.');
  disposeGroup(modelGroup); disposeGroup(selectionGroup); hideHover();
  state.document = response.document; state.selection = null; state.meshes = [];
  showCKParts();
  for (const source of state.document.meshes) {
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.Float32BufferAttribute(source.positions, 3));
    geometry.setIndex(source.indices); geometry.computeVertexNormals(); geometry.computeBoundingBox();
    const material = new THREE.MeshStandardMaterial({color: 0xae9164, metalness: .26, roughness: .72, side: THREE.DoubleSide, flatShading: true});
    const mesh = new THREE.Mesh(geometry, material); mesh.userData.block = source.block; mesh.userData.hidden = source.hidden;
    mesh.visible = !source.hidden; modelGroup.add(mesh); state.meshes.push(mesh);
  }
  $('empty').hidden = true;
  fit(); enabled();
}

function setSelection(selection) {
  for (const mesh of state.meshes) mesh.visible = !mesh.userData.hidden;
  state.selection = selection; disposeGroup(selectionGroup); hideHover();
  showCKParts();
  if (!selection) { enabled(); render(); return; }
  const areas = selection.areas || [selection];
  const selectedByBlock = new Map();
  for (const area of areas) {
    if (!selectedByBlock.has(area.block)) selectedByBlock.set(area.block, new Set());
    for (const face of area.faces) selectedByBlock.get(area.block).add(face);
  }
  // Do not draw the original pane underneath its highlight. Even a biased
  // coplanar overlay loses fragments on real plate meshes at some view angles.
  // Separate display copies keep picking, source bytes and export IDs intact.
  for (const [block, chosen] of selectedByBlock) {
    const source = state.document.meshes.find(m => m.block === block);
    const sourceMesh = state.meshes.find(m => m.userData.block === block);
    const remainder = source.indices.filter((_, i) => !chosen.has(Math.floor(i/3)));
    sourceMesh.visible = false;
    if (remainder.length) {
      const rest = sourceMesh.geometry.clone(); rest.setIndex(remainder);
      selectionGroup.add(new THREE.Mesh(rest, sourceMesh.material.clone()));
    }
  }
  const positions = areas.flatMap(area => {
    const source = state.document.meshes.find(m => m.block === area.block);
    return area.faces.flatMap(face => {
      const indices = source.indices.slice(face*3,face*3+3);
      if (area.flip) [indices[1],indices[2]] = [indices[2],indices[1]];
      return indices.flatMap(v => source.positions.slice(v*3,v*3+3));
    });
  });
  const geometry = new THREE.BufferGeometry(); geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions,3));
  const material = new THREE.ShaderMaterial({side:THREE.DoubleSide, polygonOffset:true, polygonOffsetFactor:-1, polygonOffsetUnits:-1,
    uniforms:{origin:{value:new THREE.Vector3(...selection.origin)},right:{value:new THREE.Vector3(...selection.right)},up:{value:new THREE.Vector3(...selection.up)},cell:{value:Math.max(selection.width,selection.height)/12},flipped:{value:false}},
    vertexShader:'varying vec3 point; void main(){point=position;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}',
    fragmentShader:'uniform vec3 origin;uniform vec3 right;uniform vec3 up;uniform float cell;uniform bool flipped;varying vec3 point;void main(){vec3 p=point-origin;float tile=mod(floor(dot(p,right)/cell)+floor(dot(p,up)/cell),2.0);vec3 c=mix(vec3(.21,.38,.29),vec3(.62,.76,.49),tile);if(gl_FrontFacing==flipped)c*=.6;gl_FragColor=vec4(c,1.0);}' });
  selectionGroup.add(new THREE.Mesh(geometry,material));
  const boundary = new THREE.BufferGeometry(); boundary.setAttribute('position',new THREE.Float32BufferAttribute(selection.boundary.flat(2),3));
  selectionGroup.add(new THREE.LineSegments(boundary,new THREE.LineBasicMaterial({color:0xd9f3b5,depthTest:false,transparent:true,opacity:.95})));
  enabled(); render();
}

async function choose(hit, additive = false) {
  // Geometry is already in model/world coordinates. The side actually clicked
  // becomes the reflective front, including a face whose original winding is reversed.
  const flip = hit.face.normal.dot(raycaster.ray.direction) > 0;
  await operation(async () => {
    if (additive && !state.multiArea) throw new Error('This preview server predates Shift-selection. Open the latest Mirror Creator address.');
    setSelection(await (await api('/api/select',{id:state.document.id,block:hit.object.userData.block,seed:hit.faceIndex,mode:'connected',flip,additive,selection:additive ? state.selection : null})).json());
  });
}

async function openFile(file) {
  if (!file) return;
  await operation(async () => {
    if (!file.name.toLowerCase().endsWith('.nif')) throw new Error('Select a NIF file.');
    if (file.size > 64*1024*1024) throw new Error('Select a NIF smaller than 64 MB.');
    loadResponse(await (await api('/api/import?name=' + encodeURIComponent(file.name),file,true)).json());
  });
}

$('load').onclick = () => $('nif-input').click();
$('close').onclick = async () => {
  try { await api('/api/close', {}); }
  catch (error) { message(error.message); return; }
  state.busy = true; enabled();
  message('Mirror Creator has closed. You can close this tab.');
};
$('nif-input').onchange = e => { openFile(e.target.files[0]); e.target.value=''; };
$('save').onclick = () => operation(async () => {
  const response=await api('/api/export-nif',{id:state.document.id,mirrorType:'placeable',selection:state.selection});
  const blob=await response.blob(), url=URL.createObjectURL(blob), a=document.createElement('a');
  const name=state.document.name;
  a.href=url; a.download=typeof name==='string' && name.toLowerCase().endsWith('.nif') ? name : 'mirror.nif';
  document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url),30000);
});
let dragDepth=0;
document.addEventListener('dragenter', e => {if(e.dataTransfer.types.includes('Files')){e.preventDefault();dragDepth++;$('drop-overlay').hidden=false;}});
document.addEventListener('dragover', e => {if(e.dataTransfer.types.includes('Files'))e.preventDefault();});
document.addEventListener('dragleave', () => {dragDepth=Math.max(0,dragDepth-1);if(!dragDepth)$('drop-overlay').hidden=true;});
document.addEventListener('drop',e => {e.preventDefault();dragDepth=0;$('drop-overlay').hidden=true;if(e.dataTransfer.files.length!==1)message('Drop one NIF at a time.');else openFile(e.dataTransfer.files[0]);});

try { initializeViewer(); enabled(); }
catch(error) { message('Could not start the 3D preview. Try Edge or Chrome with graphics acceleration enabled.'); }
fetch('/api/health').then(r => r.json()).then(info => { state.multiArea = info.multiAreaSelection === true; }).catch(() => {});

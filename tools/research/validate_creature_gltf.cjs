// Independent Khronos validation; reads published assets, never grants art acceptance.
// npm install --prefix assetgen_output/creatures/r6_validator --ignore-scripts
//   --no-audit --no-fund --save-exact gltf-validator@2.0.0-dev.3.10
// API: https://github.com/KhronosGroup/glTF-Validator/tree/main/node
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..');
const models=path.resolve(process.argv[2]||path.join(root,'models/creatures'));
const out=path.resolve(process.argv[3]||path.join(root,'docs/research/creature-r6-v2/khronos'));
const packageDir=path.resolve(process.argv[4]||path.join(root,'assetgen_output/creatures/r6_validator/node_modules/gltf-validator'));
const wearMode=process.argv.includes('--wear');
const validator=require(packageDir);
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
  fs.mkdirSync(out,{recursive:true});
  const result={schema:'creature-khronos-validation.v1',validator_version:validator.version(),
    package_version:require(path.join(packageDir,'package.json')).version,
    scope:(wearMode?'worn-only armor':'published creature')+' glTF buffers/images/schema; not artistic or complete R6 acceptance',
    totals:{assets:0,errors:0,warnings:0,infos:0},message_codes:{},assets:[]};
  for(const entry of fs.readdirSync(models,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))){
    if(!entry.isDirectory())continue;
    const folder=path.join(models,entry.name);const definition=path.join(folder,wearMode?'wear.json':'creature.json');
    if(!fs.existsSync(definition))continue;
    const def=JSON.parse(fs.readFileSync(definition,'utf8').replace(/^\uFEFF/,''));
    if(wearMode?!def.model:def.visual?.backend!=='gltf_skeleton')continue;
    const model=path.resolve(folder,wearMode?def.model:def.visual.gltf.model);const bytes=fs.readFileSync(model);
    const files={[path.relative(folder,model)]:digest(bytes)};
    const report=await validator.validateBytes(new Uint8Array(bytes),{
      uri:path.basename(model),format:'gltf',maxIssues:10000,
      externalResourceFunction:async uri=>{
        if(/^[a-zA-Z][a-zA-Z\d+.-]*:/.test(uri))throw new Error('External network URI forbidden: '+uri);
        const target=path.resolve(path.dirname(model),decodeURIComponent(uri));
        const relative=path.relative(folder,target);
        if(relative.startsWith('..')||path.isAbsolute(relative))throw new Error('Resource outside species folder: '+uri);
        const data=fs.readFileSync(target);files[relative]=digest(data);return new Uint8Array(data);
      }
    });
    fs.writeFileSync(path.join(out,entry.name+'.report.json'),JSON.stringify(report,null,2)+'\n');
    const issues=report.issues;
    for(const message of issues.messages||[])result.message_codes[message.code]=(result.message_codes[message.code]||0)+1;
    result.totals.assets++;result.totals.errors+=issues.numErrors;result.totals.warnings+=issues.numWarnings;result.totals.infos+=issues.numInfos;
    result.assets.push({id:entry.name,files_sha256:files,errors:issues.numErrors,warnings:issues.numWarnings,infos:issues.numInfos,truncated:issues.truncated||false});
    console.log(entry.name+': errors='+issues.numErrors+' warnings='+issues.numWarnings);
  }
  fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify(result.totals));
  if(result.totals.assets!==(wearMode?9:35)||result.totals.errors||result.assets.some(a=>a.truncated))process.exitCode=1;
}
main().catch(error=>{console.error(error);process.exitCode=1;});

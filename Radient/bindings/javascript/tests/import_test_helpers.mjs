export function sceneEntities(f, root) {
    const result = [root];
    for (let i = 0; i < result.length; ++i) {
        const count = f.value(f.scene.GetChildCount(result[i]));
        result.push(...f.value(f.scene.GetChildren(result[i], 0, count)).pChildren);
    }
    return result;
}

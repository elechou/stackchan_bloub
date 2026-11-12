/**
 * Reference values straight from bloub's own engine, to check the C port against.
 *
 *   bun tools/bloub_ref.ts <path-to-bloub>
 *
 * Prints, for every expression, the SVG matrix of both eyes exactly as
 * `engine.sample()` returns them, with the automatic gaze drift switched off
 * (`wander: 0`) and the tiny body float subtracted, so that only the pose is
 * left. Same line format as `sim dump`.
 */
import { resolve } from 'node:path'

const root = resolve(process.argv[2] ?? '../bloub')
const { BotEngine } = await import(resolve(root, 'src/bot/engine.ts'))
const { EXPRESSIONS } = await import(resolve(root, 'src/bot/expressions.ts'))
const { liveliness } = await import(resolve(root, 'src/bot/face.ts'))

const T = 1.0 // before the first scheduled blink (1.4 s)
const R = 100
const life = liveliness(T, { wander: 0 })

function show(name: string, engine: any) {
  // absolute aim with mix 0 = keep the pose's own gaze; wander 0 = no drift
  engine.setLook({ yaw: 0, pitch: 0, mix: 0, spin: 0, wander: 0 }, -10)
  const frame = engine.sample(T)
  frame.eyes.forEach((eye: any, i: number) => {
    const m = eye.matrix.match(/matrix\(([^)]+)\)/)[1].split(',').map(Number)
    // capsule path: "M{-hw} {-hh+r}A{r} {r} 0 0 1 {-hw+r} {-hh}L..."
    const nums = (eye.d as string).match(/-?\d+(\.\d+)?/g)!.map(Number)
    const hw = Math.abs(nums[0])
    const hh = Math.abs(nums[8])
    console.log(
      [
        name,
        i,
        m[0].toFixed(4),
        m[1].toFixed(4),
        m[2].toFixed(4),
        m[3].toFixed(4),
        (m[4] - life.driftX * R).toFixed(4),
        (m[5] - life.driftY * R).toFixed(4),
        (hw * 2).toFixed(4),
        (hh * 2).toFixed(4),
        eye.alpha.toFixed(3)
      ].join(' ')
    )
  })
}

for (const expr of EXPRESSIONS) {
  show(expr.id, new BotEngine(R, 'idle', null, expr))
}
show('wink', new BotEngine(R, 'wink'))
show('wide', new BotEngine(R, 'wide'))

import test from 'node:test';
import assert from 'node:assert/strict';
import { Freshness } from '../src/freshness.ts';
test('edits and new requests invalidate late replies without invalidating the other endpoint', () => {
  const gate = new Freshness();
  const left = gate.invalidate('left');
  const right = gate.invalidate('right');
  gate.invalidate('left');
  assert.equal(gate.current('left', left), false);
  assert.equal(gate.current('right', right), true);
  const edit = gate.invalidate('editor');
  gate.invalidate('editor');
  assert.equal(gate.current('editor', edit), false);
});

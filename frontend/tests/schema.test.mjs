import test from 'node:test';
import assert from 'node:assert/strict';
import { filterRows, pairTable, summary, propertyRows } from '../src/schema.ts';

test('difference filter preserves unknown states and leaves the complete result intact', () => {
  const rows = ['same', 'different', 'unread', 'failed', 'unsupported'].map(status => ({ category: 'columns', name: status, status, left: null, right: null, changed: [] }));
  assert.deepEqual(filterRows(rows, 'columns', '', true).map(row => row.status), ['different', 'unread', 'failed', 'unsupported']);
  assert.equal(filterRows(rows, 'columns', 'UNREAD', true).length, 3);
  assert.equal(rows.length, 5);
  assert.equal(summary({ complete: false, status: 'same', rows }), '比对未完整');
});
test('default pairing follows left selection but preserves explicit different right name', () => {
  assert.deepEqual(pairTable({ left: { database: 'a', table: 'old' }, right: { database: 'b', table: 'old' } }, 'left', 'new').right, { database: 'b', table: 'new' });
  assert.equal(pairTable({ left: { database: 'a', table: 'old' }, right: { database: 'b', table: 'manual' } }, 'left', 'new').right.table, 'manual');
  assert.equal(pairTable({ left: { database: 'a', table: 'same' }, right: { database: 'b', table: 'same' } }, 'right', 'manual').left.table, 'same');
  assert.equal(pairTable({ left: { database: 'a', table: '' }, right: { database: 'b', table: '' } }, 'right', 'chosen').left.table, 'chosen');
});

test('native item envelopes expand individual properties and retain long trigger definitions', () => {
  const ddl = "CREATE TRIGGER t BEFORE INSERT ON sample FOR EACH ROW SET @x = 'a  b'";
  const row = { category: 'triggers', name: 't', status: 'different', changed: ['body'], left: { name: 't', properties: { body: "SET @x = 'a  b'", definer: 'owner@localhost' }, ddl }, right: { name: 't', properties: { body: "SET @x = 'a b'", definer: 'owner@localhost' }, ddl: ddl.replace('a  b', 'a b') } };
  assert.deepEqual(propertyRows(row).map(property => [property.name, property.changed]), [['body', true], ['definer', false]]);
  assert.equal(propertyRows(row)[0].left, "SET @x = 'a  b'");
  assert.equal(row.left.ddl, ddl);
  assert.deepEqual(propertyRows({ ...row, left: null }).map(property => property.left), [undefined, undefined]);
});

test('object search retains unread, failed and unsupported category reasons', () => {
  const rows = ['unread', 'failed', 'unsupported', 'same'].map(status => ({ category: 'columns', name: '', status, left: null, right: null, changed: [], reason: 'metadata unavailable' }));
  assert.deepEqual(filterRows(rows, 'columns', 'customer', true).map(row => row.status), ['unread', 'failed', 'unsupported']);
  assert.deepEqual(filterRows(rows, 'indexes', 'customer', true), []);
});

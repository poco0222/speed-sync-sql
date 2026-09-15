"""D6 cases plugged into the existing disposable MySQL harness."""
import json


def check_alignment(sql, pair, scan, plan, batch, invoke, record):
    def planned(table, **options):
        before = sql(f'CHECKSUM TABLE d5_left.`{table}`,d5_right.`{table}`;')
        path, result = plan(table, mode='align', **options)
        assert result.get('ok'), result
        assert before == sql(f'CHECKSUM TABLE d5_left.`{table}`,d5_right.`{table}`;'), 'Preview wrote data'
        return path, result['plan']

    def apply(path, current):
        for offset in range(0, current['total'], 256):
            result = batch(path, current, offset)
            assert result.get('ok') and result['status'] == 'passed', result
        return current

    def rejected_batch(path, current, table, offset=0):
        before = sql(f'SELECT * FROM d5_right.`{table}` ORDER BY 1;')
        result = batch(path, current, offset)
        assert not result.get('ok') and result['status'] == 'failed', result
        assert result['committed'] == 0
        assert before == sql(f'SELECT * FROM d5_right.`{table}` ORDER BY 1;'), 'Failed batch changed target'
        return result

    for direction in ['left-to-right', 'right-to-left']:
        table = 'align_' + direction.replace('-', '_')
        source, target = ('d5_left', 'd5_right') if direction == 'left-to-right' else ('d5_right', 'd5_left')
        pair(table)
        sql(f'INSERT INTO {source}.{table} VALUES(1,10,100),(2,20,200); '
            f'INSERT INTO {target}.{table} VALUES(1,11,101),(3,30,300);')
        path, current = planned(table, direction=direction, fields=['id', 'value'])
        assert current['counts'] == {'add': 1, 'modify': 1, 'delete': 1}
        apply(path, current)
        assert sql(f'SELECT * FROM {target}.{table} ORDER BY id;') == '1\t10\t101\n2\t20\t9'
        assert sql(f'SELECT * FROM {source}.{table} ORDER BY id;') == '1\t10\t100\n2\t20\t200'
        record('D6 ' + direction + ': add update delete with selected fields and source unchanged')

    filters = [{'field': 'untouched', 'op': '=', 'value': '9'}]
    pair('align_scope')
    sql('INSERT INTO d5_left.align_scope VALUES(1,10,9),(2,20,9),(8,80,8); '
        'INSERT INTO d5_right.align_scope VALUES(1,11,9),(3,30,9),(8,81,8),(9,90,8);')
    path, current = planned('align_scope', filters=filters)
    apply(path, current)
    assert sql('SELECT * FROM d5_right.align_scope ORDER BY id;') == '1\t10\t9\n2\t20\t9\n8\t81\t8\n9\t90\t8'
    record('D6 shared AND scope deletes only inside rows and preserves outside rows')

    for label, seed in [('empty_source', 'INSERT INTO d5_right.{table} VALUES(1,1,9),(2,2,9);'),
                        ('empty_target', 'INSERT INTO d5_left.{table} VALUES(1,1,9),(2,2,9);'),
                        ('both_empty', '')]:
        table = 'align_' + label
        pair(table)
        if seed:
            sql(seed.format(table=table))
        path, current = planned(table)
        assert current['counts']['delete'] == (2 if label == 'empty_source' else 0)
        apply(path, current)
        assert sql(f'SELECT COUNT(*) FROM d5_right.{table};') == ('2' if label == 'empty_target' else '0')
        record('D6 complete ' + label + ' has exact plan and outcome')

    for change, mutation in [('changed', 'UPDATE d5_right.{table} SET value=99 WHERE id=2'),
                             ('deleted', 'DELETE FROM d5_right.{table} WHERE id=2'),
                             ('out_of_scope', 'UPDATE d5_right.{table} SET untouched=8 WHERE id=2')]:
        table = 'align_drift_' + change
        pair(table)
        sql(f'INSERT INTO d5_right.{table} VALUES(1,1,9),(2,2,9);')
        path, current = planned(table, fields=['value'], filters=filters)
        sql(mutation.format(table=table))
        rejected_batch(path, current, table)
        record('D6 deletion target ' + change + ': complete batch rollback')

    pair('align_new_rows')
    sql('INSERT INTO d5_right.align_new_rows VALUES(1,1,9);')
    path, current = planned('align_new_rows')
    sql('INSERT INTO d5_left.align_new_rows VALUES(1,10,9),(3,3,9); INSERT INTO d5_right.align_new_rows VALUES(2,2,9);')
    apply(path, current)
    assert sql('SELECT * FROM d5_right.align_new_rows;') == '2\t2\t9'
    record('D6 frozen source snapshot; target keys arriving after scan never enter DELETE')

    pair('align_outside_key', 'id VARCHAR(10) COLLATE utf8mb4_general_ci PRIMARY KEY,value INT,untouched INT')
    sql("INSERT INTO d5_left.align_outside_key VALUES('A',1,9); INSERT INTO d5_right.align_outside_key VALUES('a',2,8);")
    path, current = planned('align_outside_key', filters=filters)
    result = rejected_batch(path, current, 'align_outside_key')
    assert '范围外同键' in result['error']
    record('D6 collated same key outside scope conflicts without overwrite')

    pair('align_exact', 'id BIGINT UNSIGNED,code VARBINARY(8),amount DECIMAL(40,15),note LONGTEXT,stamp DATETIME(6),PRIMARY KEY(id,code)')
    sql("INSERT INTO d5_right.align_exact VALUES(18446744073709551615,X'00FF',12345678901234567890123.123456789012345,CONCAT(REPEAT('长😀',12000),CHAR(0)),'2026-09-15 08:09:10.123456'),(9007199254740993,X'',NULL,'',NULL);")
    path, current = planned('align_exact')
    assert current['counts']['delete'] == 2
    apply(path, current)
    assert sql('SELECT COUNT(*) FROM d5_right.align_exact;') == '0'
    record('D6 exact composite BIGINT binary keys and full decimal long NULL microsecond old values')

    for suffix, mutation in [('structure', 'ALTER TABLE d5_right.{table} ADD extra INT'),
                             ('failed_scan', None), ('cancelled_scan', None), ('incomplete_scan', None)]:
        table = 'align_' + suffix
        pair(table)
        sql(f'INSERT INTO d5_right.{table} VALUES(1,1,9);')
        path, current = planned(table)
        if mutation:
            sql(mutation.format(table=table))
            rejected_batch(path, current, table)
        else:
            status = json.loads((path / 'status.json').read_text())
            status['state'] = {'failed_scan': 'failed', 'cancelled_scan': 'cancelled', 'incomplete_scan': 'complete'}[suffix]
            status['complete'] = suffix != 'incomplete_scan'
            (path / 'status.json').write_text(json.dumps(status))
            result = invoke('merge-plan', path, {'taskId': current['taskId'], 'direction': 'left-to-right', 'mode': 'align'})
            assert not result.get('ok'), result
        record('D6 ' + suffix + ' rejects deletion')

    for rule in ['RESTRICT', 'NO ACTION', 'CASCADE', 'SET NULL']:
        table = 'align_fk_' + rule.lower().replace(' ', '_')
        pair(table)
        sql(f'INSERT INTO d5_right.{table} VALUES(1,1,9),(2,2,9); '
            f'CREATE TABLE d5_right.{table}_child (id INT PRIMARY KEY,parent INT, '
            f'CONSTRAINT {table}_ref FOREIGN KEY(parent) REFERENCES d5_right.{table}(id) ON DELETE {rule}); '
            f'INSERT INTO d5_right.{table}_child VALUES(1,2);')
        path, result = plan(table, mode='align', fields=['value'])
        if rule in ['RESTRICT', 'NO ACTION']:
            assert result.get('ok'), result
            assert any(table + '_ref' in w for w in result['plan']['warnings'])
            rejected_batch(path, result['plan'], table)
        else:
            assert not result.get('ok') and table + '_ref' in result['error'], result
        assert sql(f'SELECT parent FROM d5_right.{table}_child;') == '2'
        record('D6 ON DELETE ' + rule + ': safe constraint failure or explicit cross-table block')

    pair('align_trigger')
    sql('CREATE TABLE d5_right.align_audit(id INT); INSERT INTO d5_right.align_trigger VALUES(1,1,9); '
        'CREATE TRIGGER d5_right.align_delete AFTER DELETE ON d5_right.align_trigger '
        'FOR EACH ROW INSERT INTO d5_right.align_audit VALUES(OLD.id);')
    path, result = plan('align_trigger', mode='align')
    assert not result.get('ok') and 'align_delete' in result['error'], result
    assert sql('SELECT COUNT(*) FROM d5_right.align_audit;') == '0'
    assert sql('SELECT COUNT(*) FROM d5_right.align_trigger;') == '1'
    record('D6 delete trigger with cross-table side effects blocks automatic execution')

    for table, definition, engine, opts in [
            ('align_myisam', 'id INT PRIMARY KEY,value INT', 'MyISAM', {}),
            ('align_no_key', 'id INT,value INT', 'InnoDB', {'key': [], 'mode': 'browse'})]:
        pair(table, definition, engine)
        sql(f'INSERT INTO d5_right.{table} VALUES(1,1);')
        path, task_id = scan(table, **opts)
        result = invoke('merge-plan', path, {'taskId': task_id, 'direction': 'left-to-right', 'mode': 'align'})
        assert not result.get('ok'), result
        record('D6 ' + table + ' rejects unsafe alignment')

    for table, fail in [('align_batches', False), ('align_later_conflict', True)]:
        pair(table)
        sql(f'INSERT INTO d5_right.{table} WITH RECURSIVE seq AS '
            '(SELECT 1 n UNION ALL SELECT n+1 FROM seq WHERE n<600) SELECT n,n,9 FROM seq;')
        path, current = planned(table)
        assert current['counts']['delete'] == 600
        first = batch(path, current)
        assert first.get('ok') and first['committed'] == 256, first
        if fail:
            sql(f'UPDATE d5_right.{table} SET value=-1;')
            rejected_batch(path, current, table, 256)
            assert sql(f'SELECT COUNT(*) FROM d5_right.{table};') == '344'
        else:
            for offset in [256, 512]:
                result = batch(path, current, offset)
                assert result.get('ok'), result
            assert sql(f'SELECT COUNT(*) FROM d5_right.{table};') == '0'
        record('D6 600 deletions: ' + ('later conflict retains first 256 committed' if fail else 'three batches committed'))

import { useEffect, useId, useRef, useState } from 'react';
import { Check, ChevronDown } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { ChatBridgeClient, CommandResult } from '../bridge/types';
import { Button } from '../components/ui/button';
import { DropdownMenu } from '../components/ui/menu';
import { astrbotSettings } from './runtimeBridge';

type Account = NonNullable<CommandResult['botSettings']>;
const blank = { endpoint: '', source_id: '', provider_id: '', persona_id: '' };
const inputClass = 'w-full rounded-md border border-control bg-input px-3 py-2 text-body text-ink';
const resources = [
  { name: 'source', title: '模型源', id: 'source_id', fields: [
    { key: 'api_base', label: '模型 API 地址', type: 'url' },
    { key: 'api_key', label: '模型 API Key', type: 'password' }
  ] },
  { name: 'provider', title: '模型', id: 'provider_id', fields: [
    { key: 'model', label: '模型名称', type: 'text' }
  ] },
  { name: 'persona', title: '人格', id: 'persona_id', fields: [
    { key: 'system_prompt', label: '系统提示词', type: 'textarea' },
    { key: 'custom_error_message', label: '自定义出错回复', type: 'textarea' }
  ] }
] as const;

type AstrBotOperation = 'list' | 'read' | 'save' | 'create';

export function BotSettingsSection({ bridge }: { bridge: ChatBridgeClient }) {
  const [settings, setSettings] = useState(blank);
  const [revision, setRevision] = useState<number>();
  const [key, setKey] = useState('');
  const [keySync, setKeySync] = useState(false);
  const [busy, setBusy] = useState(true);
  const [status, setStatus] = useState('');
  const pending = useRef('');
  const fieldId = useId();

  const send = (operation: 'get' | 'save') => {
    const command = createCommand(`botSettings.${operation}`, operation === 'get' ? {} : {
      revision, content: JSON.stringify({ ...settings, endpoint: settings.endpoint.trim() }),
      ...(keySync ? { openapiKey: token.trim() } : {})
    });
    setBusy(true);
    setStatus(operation === 'get' ? '正在读取账号设置…' : '正在保存账号设置…');
    pending.current = command.id;
    bridge.dispatch(command);
  };
  useEffect(() => {
    const accept = (result: CommandResult) => {
      if (result.id !== pending.current) return;
      pending.current = '';
      setBusy(false);
      if (!result.ok || !result.botSettings) {
        setStatus(result.error?.message ?? '账号设置响应无效');
        return;
      }
      const account: Account = result.botSettings;
      setSettings(account.settings);
      setRevision(account.revision);
      setKeySync(account.openapiKey !== undefined);
      if (account.openapiKey !== undefined) {
        setAuthorizedEndpoint(account.settings.endpoint);
        setKey(account.openapiKey);
      }
      setStatus('账号设置已同步');
    };
    const unsubscribe = bridge.subscribeCommandResult(accept);
    const command = createCommand('botSettings.get', {});
    pending.current = command.id;
    bridge.dispatch(command);
    return unsubscribe;
  }, [bridge]);

  const setEndpoint = (endpoint: string) => {
    setSettings((previous) => ({ ...previous, endpoint }));
    setKey('');
  };
  const [authorizedEndpoint, setAuthorizedEndpoint] = useState('');
  const token = authorizedEndpoint === settings.endpoint ? key : '';

  return <>
    <div className="rounded-[10px] border border-divider bg-sidebar p-4">
      <div className="grid gap-3">
        <label htmlFor={`${fieldId}-endpoint`} className="grid gap-1 text-body">AstrBot 地址
          <input id={`${fieldId}-endpoint`} name="astrbot-endpoint" type="url" className={inputClass}
            placeholder="https://bot.example.com" value={settings.endpoint} disabled={busy} autoComplete="off"
            onChange={(event) => setEndpoint(event.target.value)} />
        </label>
        <label htmlFor={`${fieldId}-key`} className="grid gap-1 text-body">AstrBot OpenAPI Key
          <input id={`${fieldId}-key`} name="astrbot-management-key" type="password" className={inputClass}
            value={token} disabled={busy} autoComplete="off" aria-describedby={`${fieldId}-key-help`}
            onChange={(event) => { setAuthorizedEndpoint(settings.endpoint); setKey(event.target.value); }} />
        </label>
        <p id={`${fieldId}-key-help`} className="text-small text-ink-2">{keySync
          ? '保存到账号后，密钥会加密同步到同账号设备。清空并保存可移除已同步的密钥。'
          : '使用 AstrBot 中拥有 provider 和 persona 权限的 OpenAPI Key。当前服务器同步地址和配置编号。'}</p>
      </div>
    </div>
    {resources.map((resource) => <AstrBotResource key={`${settings.endpoint}/${resource.name}`} resource={resource}
      id={settings[resource.id]} changeId={(id) => setSettings((previous) => ({ ...previous, [resource.id]: id }))}
      endpoint={settings.endpoint} token={token} sourceId={settings.source_id} disabled={busy} />)}
    <div className="flex flex-wrap items-center gap-2">
      <Button disabled={busy} onClick={() => send('get')}>读取账号设置</Button>
      <Button variant="primary" disabled={busy || revision === undefined} onClick={() => send('save')}>保存到账号</Button>
      <span className="text-small text-ink-2" role="status">{status}</span>
    </div>
  </>;
}

function AstrBotResource({ resource, id, changeId, endpoint, token, sourceId, disabled }: {
  resource: typeof resources[number]; id: string; changeId(id: string): void;
  endpoint: string; token: string; sourceId: string; disabled: boolean;
}) {
  const fieldId = useId();
  const [choices, setChoices] = useState<string[]>([]);
  const [fields, setFields] = useState<Record<string, string>>({});
  const [editing, setEditing] = useState<{ id: string; create: boolean }>();
  const [status, setStatus] = useState('');
  const [pendingOperation, setPendingOperation] = useState<AstrBotOperation>();
  const busy = pendingOperation !== undefined;
  const connected = Boolean(endpoint.trim() && token.trim()) && !disabled;
  const available = connected && !busy;
  const edit = editing?.id === id ? editing : undefined;
  const selectId = (value: string) => {
    if (value === id) return;
    changeId(value);
    setEditing(undefined);
    setFields({});
    setStatus('');
  };
  const run = async (operation: AstrBotOperation) => {
    setPendingOperation(operation);
    if (operation === 'list') setChoices([]);
    setStatus(operation === 'list' ? '' : '正在联系 AstrBot…');
    try {
      const call = <T,>() => astrbotSettings<T>(endpoint.trim(), token.trim(), resource.name, operation, id, {
        api_key: '', custom_error_message: '', ...fields, source_id: sourceId
      });
      if (operation === 'list') {
        setChoices(await call<string[]>());
      } else if (operation === 'read') {
        setFields(await call<Record<string, string>>());
        setEditing({ id, create: false });
        setStatus('已从 AstrBot 读取');
      } else {
        await call<null>();
        setFields((current) => ({ ...current, api_key: '' }));
        setEditing({ id, create: false });
        setStatus('已保存到 AstrBot');
      }
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    } finally {
      setPendingOperation(undefined);
    }
  };

  return <form className="grid gap-3 rounded-[10px] border border-divider bg-sidebar p-4" aria-labelledby={`${fieldId}-title`}
    onSubmit={(event) => { event.preventDefault(); if (available && edit) void run(edit.create ? 'create' : 'save'); }}>
    <h3 id={`${fieldId}-title`} className="font-medium">{resource.title}</h3>
    <label htmlFor={`${fieldId}-id`} className="grid gap-1 text-body">{resource.title}编号
      <input id={`${fieldId}-id`} name={resource.id} className={inputClass}
        autoComplete="off" maxLength={256} value={id} disabled={busy || disabled}
        onChange={(event) => selectId(event.target.value)} />
    </label>
    <div className="flex flex-wrap gap-2">
      <DropdownMenu
        trigger={<Button disabled={!connected || (busy && pendingOperation !== 'list')}>
          读取列表 <ChevronDown size={16} aria-hidden="true" />
        </Button>}
        onOpenChange={(open) => { if (open && available) void run('list'); }}
        side="top"
        contentClassName="scroll-thin max-h-[var(--radix-dropdown-menu-content-available-height)] overflow-y-auto"
        items={choices.length ? choices.map((choice) => ({
          label: choice, onSelect: () => selectId(choice),
          trailing: choice === id ? <><span className="sr-only">，当前编号</span><Check size={16} aria-hidden="true" /></> : undefined
        })) : [{ label: pendingOperation === 'list' ? '正在读取…' : status || `暂无可选${resource.title}`,
          disabled: true, onSelect: () => undefined }]}
      />
      <Button disabled={!available || !id.trim()} onClick={() => void run('read')}>读取配置</Button>
      <Button disabled={!available || !id.trim()} onClick={() => {
        setFields({}); setEditing({ id, create: true });
        setStatus(resource.name === 'source' ? '新建 OpenAI 兼容模型源' : `新建${resource.title}`);
      }}>新建</Button>
    </div>
    {edit && resource.fields.map((field) => {
      const props = { id: `${fieldId}-${field.key}`, name: field.key, className: inputClass,
        value: fields[field.key] ?? '', disabled: busy || disabled, required: field.key !== 'api_key' && field.key !== 'custom_error_message',
        onChange: (event: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => {
          const value = event.target.value;
          setFields((previous) => ({ ...previous, [field.key]: value }));
        } };
      return <label key={field.key} htmlFor={props.id} className="grid gap-1 text-body">{field.label}
        {field.type === 'textarea' ? <textarea {...props} rows={field.key === 'system_prompt' ? 4 : 2} />
          : <input {...props} type={field.type} autoComplete="off"
            placeholder={field.key === 'api_key' && !edit.create ? '留空保留现有密钥' : undefined} />}
      </label>;
    })}
    {edit && <div><Button type="submit" variant="primary" disabled={!available}>
      {edit.create ? '创建到 AstrBot' : '保存到 AstrBot'}
    </Button></div>}
    {status && <p role="status" className="text-small text-ink-2">{status}</p>}
  </form>;
}

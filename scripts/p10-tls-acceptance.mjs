import fs from 'node:fs';
import tls from 'node:tls';

const CA = 'server-go/certs/server-lan.crt';
const WRONG_CA = 'server-go/certs/server.crt';
const checks = [];

class Client {
  constructor(name, code) {
    this.name = name;
    this.code = code;
    this.socket = null;
    this.buffer = Buffer.alloc(0);
    this.queue = [];
    this.waiters = [];
  }

  connect(caFile = CA) {
    return new Promise((resolve, reject) => {
      const socket = tls.connect({
        host: '127.0.0.1',
        port: 8888,
        ca: fs.readFileSync(caFile),
        rejectUnauthorized: true,
      }, () => {
        this.socket = socket;
        socket.setTimeout(700);
        resolve();
      });
      this.socket = socket;
      socket.on('data', (data) => this.#parse(data));
      socket.on('error', (error) => {
        for (const waiter of this.waiters.splice(0)) waiter.reject(error);
      });
      socket.on('close', () => {
        for (const waiter of this.waiters.splice(0)) waiter.reject(new Error('EOF'));
      });
      socket.once('error', reject);
    });
  }

  #parse(data) {
    this.buffer = Buffer.concat([this.buffer, data]);
    while (this.buffer.length >= 4) {
      const size = this.buffer.readUInt32BE(0);
      if (this.buffer.length < size + 4) return;
      const message = JSON.parse(this.buffer.subarray(4, size + 4).toString('utf8'));
      this.buffer = this.buffer.subarray(size + 4);
      const waiter = this.waiters.shift();
      if (waiter) waiter.resolve(message);
      else this.queue.push(message);
    }
  }

  send(message) {
    const payload = Buffer.from(JSON.stringify(message));
    const header = Buffer.alloc(4);
    header.writeUInt32BE(payload.length);
    this.socket.write(Buffer.concat([header, payload]));
  }

  recv(timeout = 4000) {
    if (this.queue.length > 0) return Promise.resolve(this.queue.shift());
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const index = this.waiters.findIndex((item) => item.timer === timer);
        if (index >= 0) this.waiters.splice(index, 1);
        reject(new Error(`${this.name} receive timeout`));
      }, timeout);
      this.waiters.push({
        timer,
        resolve: (message) => { clearTimeout(timer); resolve(message); },
        reject: (error) => { clearTimeout(timer); reject(error); },
      });
    });
  }

  async waitFor(predicate, timeout = 7000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
      try {
        const message = await this.recv(Math.min(500, deadline - Date.now()));
        if (predicate(message)) return message;
      } catch (error) {
        if (!String(error.message).includes('receive timeout')) throw error;
      }
    }
    throw new Error(`${this.name} expected message not received`);
  }

  async drain(milliseconds = 300) {
    const deadline = Date.now() + milliseconds;
    const messages = [];
    while (Date.now() < deadline) {
      try { messages.push(await this.recv(Math.min(100, deadline - Date.now()))); }
      catch { break; }
    }
    return messages;
  }

  close() {
    if (this.socket) this.socket.destroy();
    this.socket = null;
  }
}

async function login(client) {
  client.send({ type: 'login', username: client.name, user_code: client.code });
  const response = await client.waitFor((message) => message.type === 'login_ok');
  if (response.user_code !== client.code) throw new Error('identity mismatch');
  return response;
}

function expectContent(client, type, content) {
  return client.waitFor((message) => (
    message.type === type && String(message.content || '').includes(content)
  ));
}

async function run() {
  let alice = new Client('P10Alice', 'P10A001');
  let bob = new Client('P10Bob', 'P10B001');
  const charlie = new Client('P10Charlie', 'P10C001');
  try {
    await alice.connect(); await bob.connect(); await charlie.connect();
    const aliceLogin = await login(alice); await login(bob); await login(charlie);
    if (aliceLogin.is_admin !== true) throw new Error('Alice is not administrator');
    checks.push('TLS login: Alice/Bob/Charlie passed; Alice administrator');
    await alice.drain(); await bob.drain(); await charlie.drain();

    alice.send({ type: 'chat', content: '你好，P10群聊' });
    await bob.waitFor((message) => message.type === 'chat' && message.content === '你好，P10群聊');
    checks.push('Chinese room chat: passed');

    await charlie.drain();
    alice.send({ type: 'private_chat', target_user_code: 'P10B001', content: 'P10私信隔离' });
    await bob.waitFor((message) => message.type === 'private_chat' && message.content === 'P10私信隔离');
    if ((await charlie.drain(800)).some((message) => message.content === 'P10私信隔离')) {
      throw new Error('private message leaked to Charlie');
    }
    checks.push('Private message delivery/isolation: passed');

    bob.close(); await new Promise((resolve) => setTimeout(resolve, 600));
    alice.send({ type: 'private_chat', target_user_code: 'P10B001', content: 'P10离线私信' });
    await expectContent(alice, 'system', 'offline');
    bob = new Client('P10Bob', 'P10B001'); await bob.connect(); await login(bob);
    await bob.waitFor((message) => message.type === 'offline_message' && message.content === 'P10离线私信');
    checks.push('Offline private delivery + reconnect: passed');

    alice.send({ type: 'room_create', room: 'p10room', private: false });
    await expectContent(alice, 'system', 'Channel #p10room created');
    bob.send({ type: 'room_join', room: 'p10room' });
    await expectContent(bob, 'system', 'joined room p10room');
    await alice.drain(); await bob.drain();
    alice.send({ type: 'chat', content: '频道消息验收' });
    await bob.waitFor((message) => message.type === 'chat' && message.content === '频道消息验收');
    checks.push('Public channel create/join/chat: passed');
    alice.send({ type: 'room_leave' }); bob.send({ type: 'room_leave' });
    await new Promise((resolve) => setTimeout(resolve, 400));
    await alice.drain(); await bob.drain();

    alice.send({ type: 'room_create', room: 'p10secret', private: true });
    await expectContent(alice, 'system', 'Channel #p10secret created');
    bob.send({ type: 'room_join', room: 'p10secret' });
    await expectContent(bob, 'error', 'private channel');
    alice.send({ type: 'room_action', room: 'p10secret', content: 'invite', target_user_code: 'P10B001' });
    await expectContent(alice, 'system', 'Member invited');
    bob.send({ type: 'room_join', room: 'p10secret' });
    await expectContent(bob, 'system', 'joined room p10secret');
    alice.send({ type: 'room_action', room: 'p10secret', content: 'remove_member', target_user_code: 'P10B001' });
    await expectContent(bob, 'system', 'removed from #p10secret');
    alice.send({ type: 'room_action', room: 'p10secret', content: 'delete' });
    await expectContent(alice, 'system', 'was deleted');
    checks.push('Private channel reject/invite/remove/delete: passed');

    alice.send({ type: 'admin_action', content: 'mute', target_user_code: 'P10B001' });
    await expectContent(bob, 'system', 'muted by');
    bob.send({ type: 'chat', content: '禁言期间消息' }); await expectContent(bob, 'error', 'muted');
    alice.send({ type: 'admin_action', content: 'mute', target_user_code: 'P10B001' });
    await expectContent(bob, 'system', 'unmuted by');
    checks.push('Administrator mute/unmute: passed');

    alice.send({ type: 'chat', content: 'P10撤回消息' });
    const sent = await alice.waitFor((message) => message.type === 'chat' && message.content === 'P10撤回消息');
    if (!sent.message_id) throw new Error('chat message id missing');
    alice.send({ type: 'admin_action', content: 'recall', message_id: sent.message_id, command_id: 'p10-recall' });
    await alice.waitFor((message) => message.type === 'message_recalled' && message.message_id === sent.message_id);
    checks.push('Administrator recall + message id: passed');

    alice.send({ type: 'history_request', room: 'lobby', limit: 50 });
    const history = await alice.waitFor((message) => message.type === 'history_response');
    if (!history.messages.some((message) => (
      message.message_id === sent.message_id && message.recalled === true && message.content === '消息已撤回'
    ))) throw new Error('history missing recalled message');
    checks.push('Persistent history response: passed');

    alice.send({ type: 'admin_action', content: 'kick', target_user_code: 'P10C001' });
    await expectContent(charlie, 'system', 'kicked');
    await new Promise((resolve) => setTimeout(resolve, 500));
    if (!charlie.socket.destroyed) throw new Error('Charlie not disconnected after kick');
    checks.push('Administrator kick/disconnect: passed');

    alice.close(); bob.close(); charlie.close();
    const wrong = new Client('WrongCA', 'P10W001');
    try {
      await wrong.connect(WRONG_CA);
      throw new Error('wrong CA unexpectedly accepted');
    } catch (error) {
      if (error.message.includes('unexpectedly')) throw error;
      checks.push('CA validation rejects wrong certificate: passed');
    } finally { wrong.close(); }
    return { ok: true, checks };
  } finally { alice.close(); bob.close(); charlie.close(); }
}

run().then((result) => console.log(JSON.stringify(result, null, 2))).catch((error) => {
  console.error(JSON.stringify({ ok: false, checks, error: error.message }, null, 2));
  process.exitCode = 1;
});

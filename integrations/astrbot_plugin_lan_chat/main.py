import asyncio
import json
import os
import re
import ssl
import struct
from contextlib import suppress
from pathlib import Path
from uuid import uuid4

from astrbot.api import logger
from astrbot.api.event import AstrMessageEvent, MessageChain, filter
from astrbot.api.message_components import Plain
from astrbot.api.platform import (
    AstrBotMessage,
    MessageMember,
    MessageType,
    Platform,
    PlatformMetadata,
    register_platform_adapter,
)
from astrbot.api.star import Star
from astrbot.core.platform.message_session import MessageSession
from astrbot.core.platform.platform import PlatformStatus
from astrbot.core.utils.astrbot_path import get_astrbot_data_path
from watchfiles import awatch

MAX_FRAME_BYTES = 65536
USER_CODE = re.compile(r"[A-Za-z0-9]{3,16}")
LOBBY = "lobby"
REPLY_PAUSE_SECONDS = 0.55
STRUCTURED_REPLY = re.compile(r"(?m)^[ \t]*(?:[-+*][ \t]+|\d+[.)、][ \t]+|[#>|]|[：:][ \t]*$)")
REPLY_TOKENS = re.compile(
    r"(?P<protected>\x60[^\x60\n]+\x60|\[[^\]\n]+\]\((?:[^()\n]|\([^()\n]*\))*\)"
    r"|https?://[^\s<>。！？；，、」』]+(?<![.!?,;:])"
    r"|(?i:\b(?:mr|mrs|ms|dr|vs|e\.g|i\.e)\.)|\b(?:[A-Za-z]\.){2,})"
    r"|(?P<boundary>[。！？!?]+[”’」』）)\]]*|…{2,}[”’」』）)\]]*|\.+(?=\s|$)|[；;])"
)
DEFAULT_CERTIFICATE = (
    str(Path(os.environ["LOCALAPPDATA"]) / "DUSK1NG" / "LAN Chat" / "host" / "certs" / "server-lan.crt")
    if "LOCALAPPDATA" in os.environ else ""
)


async def read_frame(reader: asyncio.StreamReader) -> dict:
    size, = struct.unpack(">I", await reader.readexactly(4))
    if not 1 <= size <= MAX_FRAME_BYTES:
        raise ValueError(f"LAN Chat frame length {size} is outside 1–{MAX_FRAME_BYTES}")
    data = json.loads((await reader.readexactly(size)).decode("utf-8"))
    if not isinstance(data, dict) or not isinstance(data.get("type"), str):
        raise ValueError("LAN Chat frame must be a JSON object with a string type")
    return data


async def write_frame(writer: asyncio.StreamWriter, message: dict) -> None:
    payload = json.dumps(message, ensure_ascii=False).encode("utf-8")
    if len(payload) > MAX_FRAME_BYTES:
        raise ValueError(f"LAN Chat frame exceeds {MAX_FRAME_BYTES} bytes")
    writer.write(struct.pack(">I", len(payload)) + payload)
    await writer.drain()


def split_reply_prose(content: str):
    for paragraph in re.split(r"\n[ \t]*\n", content):
        paragraph = paragraph.strip()
        if not paragraph:
            continue
        if STRUCTURED_REPLY.search(paragraph):
            yield paragraph
            continue
        start = 0
        for token in REPLY_TOKENS.finditer(paragraph):
            boundary = token.group("boundary")
            if boundary is None or (boundary in "；;" and token.end() - start < 44):
                continue
            if part := paragraph[start:token.end()].strip():
                yield part
            start = token.end()
        if tail := paragraph[start:].strip():
            yield tail


def split_private_reply(content: str):
    boundaries = re.finditer(
        r"(?m)^ {0,3}(?P<fence>`{3,}|~{3,})[^\n]*\n[\s\S]*?"
        r"(?:^ {0,3}(?P=fence)[ \t]*\r?$|\Z)"
        r"|^[ \t]*(?P<separator>\[\[LAN_CHAT_BREAK\]\])[ \t]*\r?$",
        content,
    )
    start = 0
    for boundary in boundaries:
        yield from split_reply_prose(content[start:boundary.start()])
        if boundary.group("fence") is not None:
            yield boundary.group().strip()
        start = boundary.end()
    yield from split_reply_prose(content[start:])


class LanChatEvent(AstrMessageEvent):
    def __init__(self, message: AstrBotMessage, platform: "LanChatPlatform"):
        super().__init__(message.message_str, message, platform.meta(), message.session_id)
        self.platform = platform

    async def send(self, message: MessageChain):
        await self.platform.send_by_session(self.session, message)
        await super().send(message)


@register_platform_adapter(
    "lan_chat",
    "LAN Chat TLS room",
    default_config_tmpl={
        "host": "127.0.0.1",
        "port": 8888,
        "tls_name": "localhost",
        "certificate": DEFAULT_CERTIFICATE,
        "username": "AstrBot",
        "user_code": "BOT001",
        "online": False,
        "password": "",
        "allowed_user_codes": [],
    },
)
class LanChatPlatform(Platform):
    def __init__(self, platform_config: dict, platform_settings: dict, event_queue: asyncio.Queue):
        super().__init__(platform_config, event_queue)
        self.writer: asyncio.StreamWriter | None = None
        self.user_code = platform_config["user_code"]
        if not USER_CODE.fullmatch(self.user_code):
            raise ValueError("LAN Chat user_code must be 3–16 ASCII letters or digits")
        self.online = platform_config.get("online", False)
        if not isinstance(self.online, bool):
            raise ValueError("LAN Chat online must be a boolean")
        self.allowed_user_codes: set[str] = set()
        if self.online:
            password = platform_config.get("password")
            if not isinstance(password, str) or not password.strip() or not 8 <= len(password.encode()) <= 72:
                raise ValueError("Online LAN Chat password must be 8–72 UTF-8 bytes")
            allowed = platform_config.get("allowed_user_codes")
            if not isinstance(allowed, list) or not allowed or any(
                not isinstance(code, str) or not USER_CODE.fullmatch(code) for code in allowed
            ):
                raise ValueError("Online LAN Chat requires a nonempty allowed_user_codes list")
            self.allowed_user_codes = {code.casefold() for code in allowed}
        self.reply_locks = {code: asyncio.Lock() for code in self.allowed_user_codes}

    def meta(self) -> PlatformMetadata:
        return PlatformMetadata(
            name="lan_chat", description="LAN Chat", id=self.config["id"],
            support_streaming_message=False,
        )

    async def run(self):
        tls = ssl.create_default_context(cafile=self.config["certificate"] or None)
        while True:
            try:
                await self.run_connection(tls)
            except* (ConnectionError, TimeoutError, asyncio.IncompleteReadError):
                if not self.online:
                    raise
                logger.warning("LAN Chat connection lost, reconnecting")
            await asyncio.sleep(5)

    async def run_connection(self, tls: ssl.SSLContext):
        reader, writer = await asyncio.open_connection(
            self.config["host"], self.config["port"], ssl=tls, server_hostname=self.config["tls_name"]
        )
        try:
            login = {"type": "login", "username": self.config["username"], "user_code": self.user_code}
            if self.online:
                login = {
                    "type": "account_login",
                    "username": self.config["username"],
                    "account_password": self.config["password"],
                }
            await write_frame(writer, login)
            response = await read_frame(reader)
            if not self.online and response["type"] == "login_pending":
                logger.info(f"LAN Chat: {self.user_code} is awaiting room owner approval")
                response = await read_frame(reader)
            if response["type"] != "login_ok":
                raise RuntimeError(f"LAN Chat login failed: {response.get('content', response['type'])}")
            if self.online and response["user_code"].casefold() != self.user_code.casefold():
                raise RuntimeError("Online LAN Chat login returned a different bot user code")
            self.writer = writer
            logger.info(f"LAN Chat: connected as {self.config['username']}#{self.user_code}")
            async with asyncio.TaskGroup() as tasks:
                tasks.create_task(self.keep_alive(writer))
                while True:
                    data = await read_frame(reader)
                    if self.online and data["type"] == "dm_backlog_end":
                        await write_frame(writer, {"type": "dm_ack_batch", "message_id": data["message_id"]})
                        continue
                    accepted = self.receive(data)
                    if self.online and accepted and data["type"] == "private_chat":
                        await write_frame(writer, {
                            "type": "dm_received", "message_id": data["message_id"],
                            "target_user_code": data["user_code"],
                        })
        finally:
            self.writer = None
            writer.close()
            await writer.wait_closed()

    async def keep_alive(self, writer: asyncio.StreamWriter):
        while True:
            await asyncio.sleep(240)
            await write_frame(writer, {"type": "dm_contacts" if self.online else "users_request"})

    def receive(self, data: dict) -> bool:
        kind = data["type"]
        if kind == "error":
            logger.error(f"LAN Chat: {data['content']}")
            return False
        private_types = ("private_chat", "offline_message") if self.online else ("private_chat",)
        group = kind == "chat" and not self.online
        if (not group and kind not in private_types) or data.get("crypto"):
            return False
        fields = ("user_code", "username", "message_id", "content")
        for field in fields:
            if not isinstance(data.get(field), str) or not data[field]:
                raise ValueError(f"LAN Chat {kind}.{field} must be a nonempty string")
        sender = data["user_code"]
        if not USER_CODE.fullmatch(sender):
            raise ValueError("LAN Chat message contains an invalid user_code")
        if sender.casefold() == self.user_code.casefold():
            return False
        if self.online and sender.casefold() not in self.allowed_user_codes:
            return False
        message_text = data["content"]
        if group and (data.get("room") != LOBBY or not message_text.startswith("/")):
            return False
        if not group and str(data.get("target_user_code")).casefold() != self.user_code.casefold():
            return False

        message = AstrBotMessage()
        message.type = MessageType.GROUP_MESSAGE if group else MessageType.FRIEND_MESSAGE
        if group:
            message.group_id = LOBBY
        message.session_id = LOBBY if group else sender.lower()
        message.self_id = self.user_code
        message.message_id = data["message_id"]
        message.sender = MessageMember(user_id=sender.lower(), nickname=data["username"])
        message.message_str = message_text
        message.message = [Plain(text=message_text)]
        message.raw_message = data
        self.commit_event(LanChatEvent(message, self))
        return True

    async def send_by_session(self, session: MessageSession, message_chain: MessageChain):
        if self.online and (
            session.message_type != MessageType.FRIEND_MESSAGE
            or session.session_id.casefold() not in self.allowed_user_codes
        ):
            raise PermissionError("Online LAN Chat replies require an allowed private session")
        writer = self.writer
        if writer is None or writer.is_closing():
            raise ConnectionError("LAN Chat is disconnected")
        if any(not isinstance(part, Plain) for part in message_chain.chain):
            raise ValueError("LAN Chat adapter supports text messages only")
        content = "".join(part.text for part in message_chain.chain)
        if not content.strip():
            return
        if session.message_type == MessageType.GROUP_MESSAGE:
            if session.session_id != LOBBY:
                raise ValueError(f"LAN Chat group session must be {LOBBY}")
            message = {"type": "chat", "room": LOBBY}
        else:
            if not USER_CODE.fullmatch(session.session_id):
                raise ValueError("LAN Chat private session must be a user code")
            message = {"type": "private_chat", "target_user_code": session.session_id}
        if self.online:
            async with self.reply_locks[session.session_id.casefold()]:
                for index, part in enumerate(split_private_reply(content)):
                    if index:
                        await asyncio.sleep(REPLY_PAUSE_SECONDS)
                    if self.writer is not writer or writer.is_closing():
                        raise ConnectionError("LAN Chat disconnected while sending a split reply")
                    await write_frame(writer, dict(message, content=part, message_id=uuid4().hex))
        else:
            await write_frame(writer, dict(message, content=content, message_id=uuid4().hex))
        await super().send_by_session(session, message_chain)


class LanChatPlugin(Star):
    @filter.on_llm_request()
    async def route_online_search(self, event: AstrMessageEvent, request):
        if not isinstance(event, LanChatEvent) or not event.platform.online:
            return
        request.system_prompt = (request.system_prompt or "") + (
            "\n\n【自然语言检索】\n"
            "先判断用户是否需要检索。每轮只先调用一个最合适的工具，等结果回来再决定下一步。"
            "查时效信息、新闻、公开资料或核实来源时用 anysearch_search；"
            "阅读用户给出的公开网页时用 anysearch_extract。"
            "问梗、流行语、黑话或缩写的含义、出处、用法时先用 search_meme_wiki，"
            "只传要查的词。若上下文已有 MemeWiki 词条，直接用词条；"
            "等词库返回无结果且确实要核实时，再用 AnySearch。"
            "问候、日常聊天和稳定常识直接回答。检索仅发送必要关键词，"
            "给出可核实的来源。结果不足时如实说明。\n"
            "\n【聊天回复】\n"
            "日常对话简洁自然，每句尽量只表达一个完整意思，按正常句号、问号和叹号断句。"
            "关联紧密的分句用逗号连接。详细解释可以按要点分组，保留列表、引用、链接和代码的格式。\n"
        )

    async def initialize(self):
        request = Path(get_astrbot_data_path()) / "lan-chat-join.request"
        self.join_requests = asyncio.create_task(self.wait_for_join_requests(request))

    async def wait_for_join_requests(self, request: Path):
        async for _ in awatch(
            request.parent,
            recursive=False,
            watch_filter=lambda _, path: Path(path).name == request.name,
        ):
            if not request.exists():
                continue
            request.unlink()
            manager = self.context.platform_manager
            for platform in tuple(manager.get_insts()):
                if isinstance(platform, LanChatPlatform) and platform.status == PlatformStatus.ERROR:
                    await manager.reload(platform.config)

    async def terminate(self):
        self.join_requests.cancel()
        with suppress(asyncio.CancelledError):
            await self.join_requests
